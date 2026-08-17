# FLUX-1

A 1-bit computer: 256-bit GRID as both memory and screen, 64 KB firmware,
1-bit accumulator, and three square-wave channels as a peripheral.

FLUX-1 is a von-Neumann machine with a 256-bit GRID (32 bytes of RAM) as its
state, a 64 KB read-only program store (the "firmware"), one 16-bit program
counter, a 1-bit accumulator, a 1-bit carry and a 2-bit mode register. Every
instruction is just 8 bits — the four base opcodes NOP/SET/CLR/JMP are enough
to program it, and a two-byte extension layer adds streaming and branching.

The whole point: the GRID *is* both the screen and the memory. A 16×16 display
renders the GRID directly. There is no arithmetic hardware: the machine is a
1-bit CPU, and programs build logic (counters, adders, state machines) from
XOR and conditional jumps. The three square channels are just another device
the firmware can drive — with FM/modulation between channels.

```
                256-bit GRID (screen + RAM)
                ┌───────────────────────┐
   ROM 64KB ──► │  CPU (PC, ACC, CARRY) │ ──► 16×16 screen
   (firmware)   └───────────────────────┘ ──► 3 square channels
```

## Quick start

```
g++ -O2 -std=c++17 asm/asmflux.cpp -o asm/asmflux.exe     # assembler
g++ -O2 -std=c++17 flux.cpp flux_core.cpp -o flux.exe      # emulator

asmflux firmware/loop.asm -o firmware/loop.flux             # assemble firmware
flux firmware/loop.flux --tps 4                             # watch it run
flux firmware/counter.flux --tps 30                         # binary counter
flux firmware/adder.flux --grid-hex --steps 300             # software adder
flux firmware/music.flux --wav firmware/music.wav --secs 12 # sound to WAV
```

New to the machine? Read **[docs/PROGRAMMING.md](docs/PROGRAMMING.md)** (RU:
[PROGRAMMING.ru.md](docs/PROGRAMMING.ru.md)) — a hardware-first tutorial.

### Firmware demos

| File               | What it does                                                        |
|--------------------|---------------------------------------------------------------------|
| `firmware/loop.asm`    | Running light: SET bit, CLR bit, JMP. The smallest possible program.  |
| `firmware/counter.asm` | 8-bit binary counter built from flip-flops (XOR + permanent-1 cell) with software ripple carry — the GRID *is* the register. |
| `firmware/adder.asm`   | A software ripple-carry full adder: adds two 4-bit numbers stored in GRID bits, writing the 5-bit result back to GRID. Pure logic, bit by bit. |
| `firmware/beep.asm`    | Three-channel chord (C major) — audio is just one more peripheral.   |
| `firmware/music.asm`   | 4-chord tune (C–Am–F–G): 2-bit counter in GRID, ch2 is a 6 Hz LFO FM-modulating ch0, beat flag toggles ch1 volume. |

```
flux firmware/counter.flux --tps 30 --acc       # watch the binary counter
flux firmware/adder.flux --grid-hex --steps 300 # A=5 + B=3 -> S=8
flux firmware/beep.flux --wav firmware/beep.wav --secs 12
```

## The instruction set

Every instruction is one byte:

| bits 7..6 | mnemonic | meaning                                    |
|-----------|----------|--------------------------------------------|
| `00`      | `NOP`    | no operation                               |
| `01`      | `SET a`  | grid[a] = 1                                |
| `10`      | `CLR a`  | grid[a] = 0                                |
| `11`      | `JMP a`  | PC = (PC & 0xFFC0) \| a  (jump within the current 64-byte block) |

The assembler never emits byte `0xFC` as a base instruction, so it is safe as
an *extension prefix*: `0xFC <sub> <operands>`. These pseudo-instructions make
the machine actually programmable:

| mnemonic | encoding          | meaning                             |
|----------|-------------------|-------------------------------------|
| `GET a`  | `FC 00 a`         | ACC = grid[a]                       |
| `PUT a`  | `FC 01 a`         | grid[a] = ACC                       |
| `XOR a`  | `FC 02 a`         | ACC = ACC XOR grid[a] (toggle)      |
| `JZ a`   | `FC 03 a`         | if ACC==0 jump within 64-byte block |
| `JMPF a` | `FC 05 aL aH`     | PC = a (full 16-bit jump)           |
| `JZF a`  | `FC 06 aL aH`     | if ACC==0 PC = a                    |
| `COPY d s` | `FC 07 d sL sH` | GRID byte d = ROM[s]                |
| `SETCH c p v` | `FC 04 c p vL vH` | set channel parameter (see below) |

### Audio control (`SETCH c p v`)

| p | param       | meaning                                        |
|---|-------------|------------------------------------------------|
| 0 | `freq`      | frequency in Hz (16-bit)                       |
| 1 | `duty`      | pulse width 0..255 (0 = silent, 255 = DC)      |
| 2 | `volume`    | 0..255                                         |
| 3 | `mod_src`   | 0x00..0xFF grid bit, 0xF0..0xF2 channel, 0xFF off |
| 4 | `mod_depth` | 8.8 fixed point (256 = 1.0)                    |
| 5 | `mod_dest`  | 0 = freq, 1 = duty, 2 = volume                 |

The assembler sugar:

```
FREQ ch v     ; SETCH c 0 v
DUTY ch v     ; SETCH c 1 v
VOL  ch v     ; SETCH c 2 v
MOD  ch src depth dest   ; SETCH c 3 src, SETCH c 4 depth, SETCH c 5 dest
```

Modulation: `target = base + source * depth`, where source is the grid bit or
the output (+1/−1) of another channel. So `MOD 0 0xF2 200 0` makes channel 0's
frequency wobble by ±200/256 × 1 when channel 2 is high, giving vibrato/FM.

## Assembler directives

```
.org 0x100         ; continue at address
.byte 1, 2, 0x0F   ; emit bytes
.word 0x1234, ...  ; emit 16-bit words (LE)
.incbin "file"     ; include raw bytes
LABEL:             ; define a label
; or #             ; comment
```

Labels support arithmetic: `JMPF FRAME_TABLE+32`, `COPY 3 TABLE+1`.

## Emulator options

```
flux rom.flux [--tps N] [--frames N] [--wav out.wav] [--wav-rate HZ]
             [--secs S] [--headless] [--acc] [--grid-hex] [--steps N]
```

* `--tps N`    CPU ticks per second for animation (default 60)
* `--wav out.wav` render audio to WAV and exit (`--secs` controls length)
* `--wav-rate HZ` sample rate (default 44100; 22050 is the 8086-friendly rate)
* `--acc`      also print the channel registers (freq/duty/vol/mod)
* `--grid-hex` print all 32 GRID bytes as hex (logic-analyzer view)
* `--steps N`  run N instructions and print one frame (for testing)

## Design notes

* GRID is 256 bits = 32 bytes. Bit `y*16+x` renders at row y, column x.
* GRID doubles as program state: a "permanent 1" cell (e.g. `SET 0x0F`) plus
  `XOR` gives you a toggling flip-flop; two such cells make a 2-bit counter.
* The 64-byte block limit on `JMP`/`JZ` is a deliberate constraint to force
  self-contained blocks; `JMPF`/`JZF` lift it for full programs.
* Audio: 16-bit phase accumulator per channel, wrap at 65536; sample = ±1
  stepped by duty; summed and clipped to 16-bit PCM. WAV is mono.

## Repository layout

```
SPEC.md            the full design specification
flux_core.h/.cpp   the CPU core (FluxCPU)
flux.cpp           emulator front-end (ASCII, WAV)
asm/asmflux.cpp    the ASM-Flux assembler
firmware/          example programs (.asm + assembled .flux)
tools/             generators (adder test vectors, etc.)
legacy/            the earlier wave-machine prototype (16×16 cellular)
docs/              programming guide + language & architecture reference (EN/RU)
```

## License

MIT — see [LICENSE](LICENSE).