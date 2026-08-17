// flux_core.cpp
// Implementation of the Flux-1 CPU core.
#include "flux_core.h"

#include <cmath>

namespace flux {

// extension prefix byte: reserved; never emitted as a base JMP by the assembler
constexpr uint8_t EXT_PREFIX = 0xFC;

// sub-opcodes after the prefix
enum ExtOp : uint8_t {
    EXT_GET    = 0x00,  // GET addr        -> ACC = grid[addr]
    EXT_PUT    = 0x01,  // PUT addr        -> grid[addr] = ACC
    EXT_XOR    = 0x02,  // XOR addr        -> ACC ^= grid[addr]
    EXT_JZ     = 0x03,  // JZ addr         -> if ACC==0 PC = block|addr
    EXT_SETCH  = 0x04,  // SETCH ch param v16  -> set a channel parameter
    EXT_JMPF   = 0x05,  // JMPF addr16     -> PC = addr16 (full 16-bit jump)
    EXT_JZF    = 0x06,  // JZF addr16      -> if ACC==0 PC = addr16
    EXT_COPY   = 0x07,  // COPY dst src16  -> GRID byte dst = ROM[src16]
};

void FluxCPU::reset() {
    grid.fill(0);
    rom.fill(0);
    pc = 0;
    acc = 0;
    carry = false;
    mode = 0;
    for (auto& c : ch) {
        c.freq = 0; c.duty = 128; c.volume = 0;
        c.mod_src = 0xFF; c.mod_depth = 0; c.mod_dest = 0;
        c.phase = 0; c.last = 0;
        c.curFreq = 0; c.curDuty = 128; c.curVol = 0;
    }
    cycles_ = 0;
}

// ---------------- execution ----------------

void FluxCPU::tick() {
    uint8_t instr = rom[pc++];
    uint8_t op = (instr >> 6) & 0x03;
    uint8_t arg = instr & 0x3F;
    ++cycles_;

    // extension dispatch
    if (instr == EXT_PREFIX) {
        uint8_t sub = rom[pc++];
        ++cycles_;
        switch (sub) {
            case EXT_GET: {
                uint8_t a = rom[pc++]; ++cycles_;
                acc = getBit(a) ? 1 : 0;
                break;
            }
            case EXT_PUT: {
                uint8_t a = rom[pc++]; ++cycles_;
                setBit(a, acc != 0);
                break;
            }
            case EXT_XOR: {
                uint8_t a = rom[pc++]; ++cycles_;
                acc = (acc ^ (getBit(a) ? 1 : 0)) & 1;
                break;
            }
            case EXT_JZ: {
                uint8_t a = rom[pc++]; ++cycles_;
                if (acc == 0) pc = (pc & 0xFFC0) | (a & 0x3F);
                break;
            }
            case EXT_JMPF: {
                uint16_t a = rom[pc++]; a |= (uint16_t)rom[pc++] << 8; cycles_ += 2;
                pc = a;
                break;
            }
            case EXT_JZF: {
                uint16_t a = rom[pc++]; a |= (uint16_t)rom[pc++] << 8; cycles_ += 2;
                if (acc == 0) pc = a;
                break;
            }
            case EXT_COPY: {
                uint8_t dst = rom[pc++]; ++cycles_;
                uint16_t src = rom[pc++]; src |= (uint16_t)rom[pc++] << 8; cycles_ += 2;
                if (dst < GRID_BYTES && src < ROM_SIZE) grid[dst] = rom[src];
                break;
            }
            case EXT_SETCH: {
                uint8_t chIdx = rom[pc++]; ++cycles_;
                uint8_t param = rom[pc++]; ++cycles_;
                uint16_t v = rom[pc++]; v |= (uint16_t)rom[pc++] << 8; cycles_ += 2;
                if (chIdx < AUDIO_CHANNELS) {
                    Channel& c = ch[chIdx];
                    switch (param) {
                        case 0: c.freq = v; break;
                        case 1: c.duty = (uint8_t)v; break;
                        case 2: c.volume = (uint8_t)v; break;
                        case 3: c.mod_src = (uint8_t)v; break;
                        case 4: c.mod_depth = (int16_t)v; break;
                        case 5: c.mod_dest = (uint8_t)v; break;
                        default: break;
                    }
                }
                break;
            }
            default:
                break;   // unknown extension: no-op
        }
        return;
    }

    switch (op) {
        case OP_NOP:
            break;
        case OP_SET:
            setBit(arg, true);
            break;
        case OP_CLR:
            setBit(arg, false);
            break;
        case OP_JMP:
            pc = (pc & 0xFFC0) | (arg & 0x3F);
            break;
    }
}

void FluxCPU::run(int steps) {
    for (int i = 0; i < steps; ++i) tick();
}

// ---------------- audio ----------------

// Modulation (SPEC.md 7.2): source = grid bit (0/1) or channel output (-1..1).
// target = freq / duty / volume.  Algorithm: dst += mod_val * depth.
// mod_depth is 8.8 fixed point; the value here is the *shift per sample* in
// the corresponding parameter units (Hz / duty steps / volume steps).
void FluxCPU::applyModulation() {
    for (auto& c : ch) {
        float baseF = (float)c.freq;
        float baseD = (float)c.duty;
        float baseV = (float)c.volume;
        float off = c.mod_depth / 256.0f;   // 8.8 fixed -> float

        if (c.mod_src != 0xFF) {
            float src = 0.0f;
            if (c.mod_src >= 0xF0 && c.mod_src <= 0xF2) {
                int ci = c.mod_src & 0x03;
                src = ch[ci].last;                       // -1..1
            } else {
                src = getBit(c.mod_src) ? 1.0f : 0.0f;   // grid bit
            }
            switch (c.mod_dest) {
                case 0: baseF += src * off; break;
                case 1: baseD += src * off; break;
                case 2: baseV += src * off; break;
            }
        }
        if (baseF < 0) baseF = 0;
        if (baseD < 0) baseD = 0; if (baseD > 255) baseD = 255;
        if (baseV < 0) baseV = 0; if (baseV > 255) baseV = 255;
        c.curFreq = baseF;
        c.curDuty = baseD;
        c.curVol  = baseV;
    }
}

// One mixed sample. Uses the phase accumulator (SPEC.md 7.1).
float FluxCPU::renderSample() {
    applyModulation();

    float mix = 0.0f;
    for (auto& c : ch) {
        if (c.curFreq <= 0.0f || c.curVol <= 0.0f) {
            c.last = 0.0f;
            continue;
        }
        // 65536-step phase wrap, SPEC.md 7.1
        float t = c.phase / 65536.0f;
        float duty = c.curDuty / 255.0f;
        float val = (t < duty) ? 1.0f : -1.0f;
        c.phase += c.curFreq * 65536.0f / DEF_SAMPLE_RATE;
        if (c.phase >= 65536.0f) c.phase -= 65536.0f;
        c.last = val * (c.curVol / 255.0f);
        mix += c.last;
    }
    if (mix > 1.0f) mix = 1.0f;
    if (mix < -1.0f) mix = -1.0f;
    return mix;
}

void FluxCPU::renderBlock(float* out, int frames) {
    for (int i = 0; i < frames; ++i) out[i] = renderSample();
}

} // namespace flux