// flux_core.h
// Flux-1 CPU core: 1-bit streaming computer with three audio channels.
// Per SPEC.md:
//   GRID  : 256 bits (32 bytes) of data memory, addr 0x00..0xFF
//   ROM   : up to 64 KB program memory, addressed by PC (16 bit)
//   PC    : program counter
//   ACC   : 1-bit accumulator
//   CARRY : 1-bit carry flag
//   MODE  : 2-bit mode register
// Instruction set (8-bit): bits 7-6 opcode, bits 5-0 6-bit argument.
//   00 NOP | 01 SET | 10 CLR | 11 JMP (PC = (PC & 0xFFC0) | arg)
// Two-byte extensions (SPEC.md "Расширение"): reserved prefix byte 0xFC
// followed by a sub-opcode byte and its operands:
//   GET addr | PUT addr | XOR addr | JZ addr | SETCH ch param v16
// The assembler never emits 0xFC as a base JMP, so base semantics of the
// four instructions stay exactly as specified.
#pragma once

#include <cstdint>
#include <array>
#include <cstddef>

namespace flux {

constexpr size_t GRID_BYTES   = 32;       // 256 bits
constexpr size_t GRID_BITS    = GRID_BYTES * 8;
constexpr size_t ROM_SIZE     = 65536;    // 64 KB
constexpr int    AUDIO_CHANNELS = 3;
constexpr float  DEF_SAMPLE_RATE = 44100.0f;

// ---- opcodes (2 high bits) ----
enum Op : uint8_t { OP_NOP = 0, OP_SET = 1, OP_CLR = 2, OP_JMP = 3 };

// ---- grid helpers ----
inline int grid_x(uint8_t addr) { return addr & 0x0F; }
inline int grid_y(uint8_t addr) { return (addr >> 4) & 0x0F; }

// ---- one audio channel ----
struct Channel {
    uint16_t freq      = 0;      // note: 0..65535 (0 = off)
    uint8_t  duty      = 128;    // pulse width 0..255 (128 = 50%)
    uint8_t  volume    = 0;      // 0..255 (linear)
    uint8_t  mod_src   = 0xFF;   // 0x00..0xFF grid bit, 0xF0..0xF2 channel, 0xFF = off
    int16_t  mod_depth = 0;      // 8.8 fixed point modulation depth
    uint8_t  mod_dest  = 0;      // what the modulator drives:
                                 //   0 = freq, 1 = duty, 2 = volume
    float    phase     = 0.0f;   // phase accumulator (samples*65536 wrap)

    // per-sample parameter snapshots after modulation (freq used for phase)
    float    curFreq  = 0.0f;
    float    curDuty  = 128.0f;
    float    curVol   = 0.0f;

    // previous output sample (-1..1); usable as a modulation source
    float    last     = 0.0f;
};

// ---- the CPU ----
class FluxCPU {
public:
    FluxCPU() { reset(); }

    void reset();

    // memory / registers
    std::array<uint8_t, GRID_BYTES> grid;
    std::array<uint8_t, ROM_SIZE>   rom;
    uint16_t pc     = 0;
    uint8_t  acc    = 0;    // 1-bit accumulator (masked to 0/1)
    bool     carry  = false;
    uint8_t  mode   = 0;    // 2-bit mode register

    std::array<Channel, AUDIO_CHANNELS> ch;

    // ---- memory access ----
    inline bool getBit(uint8_t addr) const {
        return (grid[addr >> 3] >> (addr & 7)) & 1;
    }
    inline void setBit(uint8_t addr, bool v) {
        if (v) grid[addr >> 3] |=  (uint8_t)(1u << (addr & 7));
        else   grid[addr >> 3] &= (uint8_t)~(1u << (addr & 7));
    }

    // ---- execution ----
    void tick();                    // one CPU instruction
    void run(int steps);            // run N instructions

    // ---- audio ----
    float renderSample();           // one mixed sample (after applying modulation)
    void  renderBlock(float* out, int frames);

    // register/counter access used by emulator front-end
    uint64_t cycles() const { return cycles_; }
    void     setCycles(uint64_t c) { cycles_ = c; }

private:
    void applyModulation();
    uint64_t cycles_ = 0;
};

} // namespace flux