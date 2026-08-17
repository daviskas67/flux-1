# FLUX-1 Architecture

FLUX-1 is a tiny von-Neumann computer whose single memory IS the screen, and
whose three square-wave channels turn that memory into sound.

## Machine model

```
        ┌──────────────────────────────────────────────┐
        │              FLUX-1 CPU                       │
        │   PC ──16 bit──► address                      │
        │   │                                           │
        │   ▼                                           │
        │  ROM (64 KB, firmware, read-only)  ──decoded─►┼──► GRID write
        │                                               │
        │   GRID: 256 bits = 32 bytes of RAM            │
        │   ACC: 1 bit      CARRY: 1 bit   MODE: 2 bit  │
        └──────────────────────────────────────────────┘
              │ grid bits                       │ channel params
              ▼                                 ▼
        16×16 display (ASCII/SDL)        3 square channels
                                          phase accumulators
                                          FM / PWM / tremolo
                                              │
                                              ▼
                                         mix → WAV / audio
```

## State

| register | width | meaning                                        |
|----------|-------|------------------------------------------------|
| `grid`   | 256 b | RAM + screen: bit `y*16+x` = pixel (x,y)       |
| `rom`    | 64 KB | firmware, read-only                            |
| `pc`     | 16 b  | program counter                                |
| `acc`    | 1 b   | accumulator (used by GET/PUT/XOR/JZ/JZF)       |
| `carry`  | 1 b   | reserved for arithmetic extensions             |
| `mode`   | 2 b   | reserved for mode switching                    |
| `ch[3]`  | —     | audio channels (see Audio)                     |

## Execution model

The CPU fetches one byte from `rom[pc]` and executes it, then increments `pc`
(except on jumps). Each instruction takes at least 1 "cycle"; extension bytes
add cycles so that audio rate stays meaningful.

### Base instructions

* `NOP` — nothing.
* `SET a` — `grid[a] = 1`.
* `CLR a` — `grid[a] = 0`.
* `JMP a` — `pc = (pc & 0xFFC0) | a`. Only the low 6 bits are used, so a jump
  cannot leave the current 64-byte block. This is a deliberate constraint: it
  keeps blocks self-contained and forces small, simple control flow.

### Extensions

`0xFC` is reserved as an escape. Because SET/CLR/JMP encode `0x40..0xFF` only,
`0xFC` never collides. After `0xFC`, the next byte is a sub-opcode:

| sub | op       | effect                                              |
|-----|----------|-----------------------------------------------------|
| 0x00| `GET a`  | `acc = (grid[a>>3] >> (a&7)) & 1`                    |
| 0x01| `PUT a`  | set/clear `grid[a]` from `acc`                       |
| 0x02| `XOR a`  | `acc ^= grid bit a`                                  |
| 0x03| `JZ a`   | if `acc == 0` then block-local jump                  |
| 0x04| `SETCH c p v` | channel parameter set                          |
| 0x05| `JMPF a` | `pc = a` (full 16-bit)                               |
| 0x06| `JZF a`  | if `acc == 0` then `pc = a`                          |
| 0x07| `COPY d s` | `grid[d] = rom[s]` (stream ROM data into GRID)     |

`SETCH` writes one parameter of one channel:

```
0xFC 0x04 channel param vL vH
param: 0=freq 1=duty 2=volume 3=mod_src 4=mod_depth 5=mod_dest
```

## The 64-byte block rule

The GRID is 32 bytes, so the low 6 bits of any address select a bit. JMP
reuses the current `pc & 0xFFC0` (the containing 64-byte block) as the high
bits — a block-local jump. Programs larger than 64 bytes use `JMPF`/`JZF`
(full 16-bit). The `.org` directive places firmware tables anywhere in ROM.

## Programming patterns

### Toggle / counter

A "permanent 1" cell (never cleared) plus `XOR` flips a bit each pass:

```
SET  0x0F        ; permanent 1
GET  0x00        ; acc = bit0
XOR  0x0F        ; acc = bit0 ^ 1
PUT  0x00        ; bit0 = acc  → bit0 toggles every pass
JZF  CARRY       ; if bit0 became 0 → ripple carry
JMPF DONE
```

Two cells make a 2-bit counter; `music.asm` uses one to walk a 4-chord
progression (C–Am–F–G).

### Sound as state

A channel's frequency/duty/volume are plain parameters that any code can set
with `SETCH`. A free-running LFO is just a channel with a low frequency
modulating another channel's frequency (FM) or volume (tremolo). Grid bits
modulate audio; audio modulates audio.

## Audio

### Square synthesis

Each channel has:

```
phase  : 16-bit accumulator (0..65535)
freq   : Hz (16-bit)
duty   : 0..255 pulse width
volume : 0..255
mod_src/mod_depth/mod_dest : modulation
```

Per output sample (rate = 44100 Hz default, 22050 for 8086):

```
phase += freq * 65536 / rate        (wraps at 65536)
sample = (phase < duty * 256) ? +1 : -1
out    = sample * volume / 255
```

### Modulation

```
base   = freq or duty or volume (mod_dest)
src    = grid bit (0/1) | channel output (−1/+1) | off (0)
value  = base + src * mod_depth / 256
```

Modulation is applied before the sample is produced; depth is 8.8 fixed point
(256 = 1.0). Examples: `MOD 0 0xF2 200 0` → ch0 freq modulated by ch2's square
output (±200/256 Hz wobble, i.e. FM); `MOD 1 0x10 120 2` → ch1 volume driven
by grid bit 0x10 (beat flag).

### Mixing

The three channels sum to `-3..+3`, are clipped to `-1..+1`, then scaled to
16-bit signed PCM. WAV output is mono 16-bit.

## Rendering

The emulator prints the GRID as 16×16 with ANSI colors: green `█` for a set
bit, dark `░` for a cleared bit. `--acc` also prints per-channel registers.
`--steps N` runs N instructions then prints one frame — useful for debugging.

## Performance / porting notes

* The core (`FluxCPU::tick`) is branch-free except for jumps; audio renders in
  blocks, so WAV export is fast.
* 8086 port plan: 22050 Hz sample rate, 16-bit integers, GRID as 32 bytes,
  ROM from `firmware/*.flux`. See SPEC.md for the DOS roadmap.