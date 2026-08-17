# Flux-1 Language Reference — `.flux`

The Flux-1 programming language is a **wave map**: instead of writing linear
instructions, you program a 16×16 grid of cells with *rules*, seed it with
*bits and accumulators*, and watch *waves* propagate across the matrix like ants.

This document is the complete, authoritative reference for writing `.flux`
programs. For the execution model, see [ARCHITECTURE.md](ARCHITECTURE.md).

---

## Table of Contents

1. [Lexical conventions](#1-lexical-conventions)
2. [Program structure](#2-program-structure)
3. [Coordinates](#3-coordinates)
4. [Global directives](#4-global-directives)
5. [State seeding](#5-state-seeding)
6. [Conditions](#6-conditions)
7. [Actions](#7-actions)
8. [Rules](#8-rules)
9. [Audio channels & FM synthesis](#9-audio-channels--fm-synthesis)
10. [CLI reference](#10-cli-reference)
11. [Complete examples](#11-complete-examples)
12. [Design patterns](#12-design-patterns)
13. [Grammar](#13-grammar)

---

## 1. Lexical conventions

* A **comment** starts with `#` and runs to the end of the line.
* A line may contain multiple **statements** separated by `;`.
* The characters `:`, `,`, `=` are treated as whitespace (token separators).
  They are commonly used for readability: `cell 3 5 : bit1 send E 2`.
* All keywords and identifiers are **lowercase**.
* Numbers may be integers (`steps 100`) or floats (`freq 0 261.63`).
* Everything is whitespace/`;`-delimited — indentation is not significant.

```flux
# this whole line is a comment
cell 0 0 : always emit E 1   # trailing comment is fine
steps 200
```

---

## 2. Program structure

A `.flux` program is an ordered list of directives. Order matters only in the
sense that later `cell`/`rule` directives append rules to cells. Execution
(order of evaluation within a tick) is defined in ARCHITECTURE.md.

| Directive        | Purpose                                        |
|------------------|------------------------------------------------|
| `steps N`        | number of ticks to simulate                    |
| `pause MS`       | animation delay per frame                      |
| `rate HZ`        | audio sample rate                              |
| `sps N`          | audio samples per grid tick                    |
| `chan I ...`     | configure square channel I                     |
| `seed X Y`       | set a data bit                                 |
| `seedacc X Y`    | set an accumulator bit                         |
| `inject X Y DIR B D` | inject a travelling wave                   |
| `cell X Y : ...` | add rules to one cell                          |
| `rule ...`       | add global rules to all rule-less cells        |

---

## 3. Coordinates

The grid is **16×16** cells, index `X` in `0..15` (columns, West→East),
`Y` in `0..15` (rows, North→South).

```
 Y
 0  . . . . . . . . . . . . . . . .
 1  . . . . . . . . . . . . . . . .
 2  . . . . . . . . . . . . . . . .
 ...
15  . . . . . . . . . . . . . . . .
    0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15   X
```

Out-of-bounds coordinates are silently ignored (rules) or die (waves).

---

## 4. Global directives

### `steps N`

Sets how many synchronous ticks the simulation runs. Default `100`.

```flux
steps 400
```

### `pause MS`

Sets the per-frame animation delay in milliseconds. Default `60`. Has no
effect in `--quiet` mode. Use `--pause` on the CLI to override.

```flux
pause 120
```

### `rate HZ`

Sets the audio sample rate. Default `44100`.

```flux
rate 48000
```

### `sps N`

Sets how many audio samples are rendered **per grid tick**. Default `44100/60`
(≈735, i.e. roughly 60 grid ticks per second of sound). Smaller values make
the grid tick faster (musically: each tick is shorter).

```flux
sps 1470     # ~30 ticks/second
```

The relationship: `seconds_per_tick = sps / rate`.

### `chan I ...`

Configures square audio channel `I` (`0..2`). Channel parameters are *initial*
values; the grid can change them at runtime via `freq`/`amp`/`phase`/`mod`
actions (see §7). Syntax:

```
chan I freq F amp A [mod J D]
```

* `freq F` — carrier frequency in Hz (0 = silent channel).
* `amp A` — amplitude in `0..1`.
* `mod J D` — optional: modulate channel I with channel J, FM depth D Hz.

Example — a classic FM clarinet stack:

```flux
chan 0 square freq 55   amp 0.5           # LFO / modulator
chan 2 square freq 220  amp 0.6 mod 0 400 # carrier 220 Hz, FM by chan0
```

---

## 5. State seeding

### `seed X Y`

Sets the **data bit** of cell (X,Y) to 1. A seeded bit with no rules simply
sits there; give the cell rules to make it travel.

```flux
seed 4 4
```

### `seedacc X Y`

Sets the **accumulator** of cell (X,Y) to 1. Accumulators persist forever and
render as `A` with `--acc`.

```flux
seedacc 8 8
```

### `inject X Y DIR B D`

Injects a **wave** at (X,Y): data bit `B` (0/1), initial direction `DIR`,
and a *travel delay* of `D` ticks. A delay of 0 means the wave is actionable
on the very next tick. Direction is advisory; a cell's own `send DIR` rule
overrides it (unless the rule uses `H`).

```flux
inject 0 2 E 1 0   # a 1-bit starts moving East from (0,2), no delay
```

---

## 6. Conditions

A rule fires only when its **condition** matches the cell's current state.

| Condition | Matches when                             |
|-----------|------------------------------------------|
| `always`  | always                                   |
| `bit1`    | the cell holds a data bit                |
| `bit0`    | the cell holds no data bit               |
| `acc1`    | the accumulator is 1                     |
| `acc0`    | the accumulator is 0                     |

Conditions are evaluated against the state at the **start** of the tick
(see ARCHITECTURE.md for the two-phase model).

```flux
cell 3 3 : bit1 send E 1      # when a bit arrives here, send it East
rule acc1 toggleacc           # globally: every cell with acc=1 flips it
```

---

## 7. Actions

Actions are the "verbs" of Flux-1. They are attached to rules (§8).

### Wave control

| Action        | Arguments     | Effect                                             |
|---------------|---------------|----------------------------------------------------|
| `send DIR D`  | `DIR`, `D`    | push the cell's bit to the neighbor in `DIR`; it lands after `D` ticks. Consumes the bit. If `DIR` is `H`, keep the cell's own current direction. Out-of-bounds → wave dies. |
| `emit DIR D`  | `DIR`, `D`    | *generator*: assert a 1 toward `DIR`, landing after `D` ticks. Does **not** consume the cell's own bit — the emitter keeps working. |
| `stop`        | —             | absorb the bit here (it dies).                     |

### Accumulator ops

| Action      | Arguments | Effect              |
|-------------|-----------|---------------------|
| `setacc V`  | `V` (0/1) | accumulator = V     |
| `clracc`    | —         | accumulator = 0     |
| `toggleacc` | —         | accumulator = !acc  |

Accumulator ops are **non-consuming**: they do not stop the wave.

### Audio / modulation ops

| Action   | Arguments   | Effect                                        |
|----------|-------------|-----------------------------------------------|
| `freq I F`  | channel, Hz | set channel I's carrier frequency             |
| `amp I A`   | channel, 0..1 | set channel I's amplitude                   |
| `phase I R` | channel, rad | add R radians to channel I's phase           |
| `mod I J D` | chan, src, Hz | channel I is FM-modulated by channel J, depth D Hz |

All audio ops are non-consuming.

---

## 8. Rules

A rule is the unit of program. Two forms:

### Per-cell rule

```
cell X Y : COND ACTION [ARGS...]
```

Applies only to cell (X,Y). Multiple `cell` lines for the same coordinates
append multiple rules (first match wins — see ARCHITECTURE.md).

```flux
cell 6 4 : bit1 send E 1
cell 6 4 : bit1 setacc 1
```

### Global rule

```
rule COND ACTION [ARGS...]
```

Applies to **every cell that has no per-cell rules of its own**. Useful for
homogeneous programs (a wire everywhere, a storm everywhere).

```flux
# make the whole grid forward bits East
rule bit1 send E 1
```

### How rules are applied

1. A cell with a **data bit and a pending delay** is "charging": it counts
   down, moves nowhere, and does not evaluate rules.
2. Otherwise each rule is tried **in order**; the **first** rule whose
   condition matches and whose action is *consuming* wins. Non-consuming
   actions (acc ops, audio ops) run and then the search continues.
3. If the bit is consumed (`send`, `stop`) the cell is empty next tick.
4. If no rule consumes, the bit **stays** (a lingering wave).
5. `emit` never consumes, so an emitter keeps generating every tick.

---

## 9. Audio channels & FM synthesis

Flux-1 ships 3 **square-wave** channels. They are deliberately unrestricted:
frequency, amplitude, phase and FM routing can all be changed *by the grid*
at any tick — that is the point. A wave passing through a cell can trigger
`freq`, `amp`, `phase`, `mod` actions and reshape the sound in real time.

### Square wave

Each channel produces a square (binary) waveform from a phase accumulator:

```
instantaneous_freq = freq + modDepth * sample(modulator_channel)
phase += instantaneous_freq * 2π * (sps / rate)
sample = amp * (sin(phase) >= 0 ? +1 : -1)
```

### FM synthesis

With `mod I J D`, channel I's *instantaneous frequency* is shifted by
`D * sample(J)`. When J is an audio-rate square wave, this produces classic
FM sidebands (metallic/brassy timbres). With a low-frequency J it acts as
vibrato.

```flux
chan 0 square freq 220 amp 0.6 mod 2 200   # carrier, modulated by chan2
chan 2 square freq 55  amp 0.8             # modulator at audio-ish rate
```

### Sequencing from the grid

Because `freq`/`amp`/`mod` are non-consuming actions, a wave can hop through
a row of cells each setting a note:

```flux
# wave travels East on row Y=0; each cell it visits sets a channel frequency
cell 2 0 : bit1 freq 0 261.63    # C4
cell 4 0 : bit1 freq 0 329.63    # E4
cell 6 0 : bit1 freq 0 392.00    # G4
```

Waves and sound are synchronous: **one grid tick renders `sps` samples**.

---

## 10. CLI reference

```
flux1 program.flux [options]
```

| Option        | Meaning                                          |
|---------------|--------------------------------------------------|
| `--acc`       | render accumulators as `A`                       |
| `--ticks N`   | override the number of ticks                     |
| `--steps N`   | print every N-th frame during animation          |
| `--pause MS`  | override per-frame animation delay               |
| `--quiet`     | no animation; print only the final frame         |
| `--wav FILE`  | write the rendered audio to FILE (mono 16-bit WAV) |
| `-h`, `--help`| print usage                                      |

Examples:

```bash
# animate a serpent
flux1 examples/serpent.flux

# run a storm, showing accumulator trails, single final frame
flux1 examples/storm.flux --acc --quiet

# FM synthesis straight to audio
flux1 examples/fm.flux --wav fm.wav
```

### Render legend

| Glyph | Meaning                                   |
|-------|-------------------------------------------|
| `.`   | empty cell                                |
| `#`   | data bit, actionable now (delay 0)        |
| `o`   | data bit, charging (delay > 0)            |
| `A`   | accumulator set (only shown with `--acc`) |

The final line prints `tick=N  waves=K`.

---

## 11. Complete examples

### 11.1 Pulse train (generator + global wire)

```flux
# a generator at the corner emits 1s East; the grid forwards them
steps 32
cell 0 0 : always emit E 1
rule always send E 1
```

### 11.2 Single wave tracing a serpentine path

```flux
# one bit, clean serpentine route; every cell has exactly one rule
steps 130
inject 0 0 E 1 0
rule bit1 send E 2                    # default: head East
cell 15 0 : bit1 send S 2             # turn down at the right edge
cell 15 1 : bit1 send W 2
cell 14 1 : bit1 send S 2
...
```

### 11.3 AND gate from two streams

```flux
# two streams meet at (8,4); acc is set only if both arrive
inject 3 3 E 1 0
inject 3 5 E 1 0
cell 6 4 : bit1 send E 1              # merge point routes both East
cell 8 4 : bit1 setacc 1
cell 8 4 : bit1 stop
```

### 11.4 Memory trail ("Turing storm")

```flux
# every hop toggles the accumulator it leaves -> persistent trail
steps 60
inject 0 2 E 1 0
rule bit1 toggleacc
rule bit1 send E 2
```

### 11.5 FM clarinet with a grid-sequenced LFO

```flux
rate 44100
sps 735
cell 0 5 : bit0 emit E 0              # emitter
cell 1 5 : bit1 send E 0              # wire
cell 3 5 : bit1 freq 0 55             # LFO steps
cell 4 5 : bit1 freq 0 110
cell 5 5 : bit1 freq 0 165
cell 6 5 : bit1 freq 0 220
chan 0 square freq 55  amp 0.5
chan 2 square freq 220 amp 0.6 mod 0 400
steps 400
```

---

## 12. Design patterns

| Pattern            | Recipe                                                    |
|--------------------|-----------------------------------------------------------|
| **Generator**      | `emit DIR D` in a rule-less cell (e.g. `always emit E 1`)  |
| **Wire**           | `rule bit1 send DIR D` (global) or `cell X Y : bit1 send DIR D` |
| **Turn**           | per-cell `send` with a different `DIR`                     |
| **Traffic shaping**| `delay D` in `send`/`emit` staggers arrivals               |
| **Collision guard**| cells with one rule each; first wave wins per ARCHITECTURE |
| **Logic**          | `setacc`/`toggleacc` at gate cells; combine arrival streams |
| **Memory**         | `toggleacc` on the leaving cell => a trail                 |
| **Sequencer**      | row of cells each `freq I F` / `amp I A` / `mod I J D`     |
| **FM stack**       | low channel = modulator (`mod J D` on a carrier)           |
| **Gate**           | `stop` to absorb a wave, `acc1`/`acc0` to branch on state  |

---

## 13. Grammar

```
program     := directive*
directive   := steps-decl | pause-decl | rate-decl | sps-decl
             | chan-decl | seed-decl | seedacc-decl | inject-decl
             | cell-decl | rule-decl
steps-decl  := 'steps' INT
pause-decl  := 'pause' INT
rate-decl   := 'rate'  NUM
sps-decl    := 'sps'   INT
chan-decl   := 'chan' INT 'freq' NUM 'amp' NUM ('mod' INT NUM)?
seed-decl   := 'seed'    INT INT
seedacc-decl:= 'seedacc' INT INT
inject-decl := 'inject' INT INT DIR INT INT
cell-decl   := 'cell' INT INT ':' rule-body
rule-decl   := 'rule' rule-body
rule-body   := COND ACTION args
COND        := 'always' | 'bit1' | 'bit0' | 'acc1' | 'acc0'
ACTION      := 'send'  DIR (INT)?          # send DIR [delay]
             | 'emit'  DIR (INT)?          # emit DIR [delay]
             | 'stop'
             | 'setacc' INT
             | 'clracc'
             | 'toggleacc'
             | 'freq'  INT NUM             # freq channel Hz
             | 'amp'   INT NUM             # amp  channel 0..1
             | 'phase' INT NUM             # phase channel rad
             | 'mod'   INT INT NUM         # mod channel src depthHz
DIR         := 'N' | 'E' | 'S' | 'W' | 'H'
INT         := [+-]?[0-9]+
NUM         := [+-]?[0-9]+('.'[0-9]+)?
```