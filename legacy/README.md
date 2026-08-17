# Flux-1 — 1-bit Turing storm

Not just one bit. It's a **massively parallel 1-bit processor**: a **16×16 grid
of 256 compute cells** where bits travel like ants — *waves* racing across the
matrix, each cell a memory element, a tiny ALU and a router in one.

On top of the grid: **3 square-wave audio channels** that are deliberately
**unrestricted** — frequency, amplitude, phase and FM routing are all set *from
the waves themselves*. Tune the parameters right and you get honest FM
synthesis. The grid *is* the sequencer.

> Русская версия: [README.ru.md](README.ru.md)
> Документация / Docs: [LANGUAGE.md](docs/LANGUAGE.md) ·
> [ARCHITECTURE.md](docs/ARCHITECTURE.md)

---

## Highlights

* **256 cells** (16×16), each with a 1-bit data register, a persistent 1-bit
  accumulator, and its own tiny rule-program.
* **Wave programming**: you write a *wave map*, not linear code — "if your bit
  is 1, pass it to your East neighbor in 2 ticks".
* **3 square channels with FM**: `chan 2 square freq 220 amp 0.6 mod 0 400`.
  Waves can rewrite any channel parameter at any tick — sequencers, LFOs,
  vibrato and full FM clarinets emerge from the grid.
* **Synchronous two-phase ticks** (compute → commit), like real digital logic.
* **WAV export** (`--wav out.wav`) — mono 16-bit audio, rendered in lockstep
  with the grid.

---

## Build & run

Requires a C++17 compiler (g++ / clang++ / MSVC).

```bash
g++ -O2 -std=c++17 main.cpp flux1.cpp -o flux1

# animate a serpent
./flux1 examples/serpent.flux

# storm with accumulator trails, single final frame
./flux1 examples/storm.flux --acc --quiet

# FM synthesis straight to a file
./flux1 examples/fm.flux --wav fm.wav

# chiptune arpeggio from three wave rows
./flux1 examples/arp.flux --wav arp.wav
```

### CLI options

```
flux1 program.flux [--acc] [--ticks N] [--steps N] [--pause MS] [--quiet] [--wav out.wav]
```

| Option      | Meaning                                        |
|-------------|------------------------------------------------|
| `--acc`     | render accumulators as `A`                     |
| `--ticks N` | override the number of ticks                   |
| `--steps N` | print every N-th frame during animation        |
| `--pause MS`| override per-frame delay                       |
| `--quiet`   | no animation; print only the final frame       |
| `--wav FILE`| write rendered audio (mono 16-bit WAV)         |

---

## Language in 60 seconds

```flux
steps 200                       # ticks to simulate
cell 0 0 : always emit E 1      # generator: keep emitting 1s East
rule bit1 send E 2              # everywhere: forward bits East, 2-tick hop
rule bit1 toggleacc             # every hop leaves an accumulator trail
chan 2 square freq 220 amp 0.6 mod 0 400   # FM clarinet
```

Full reference: [docs/LANGUAGE.md](docs/LANGUAGE.md)

## Model in 30 seconds

* Each tick is two phases: **A — evaluate** every cell, **B — commit** all
  moves simultaneously. Accumulators survive ticks.
* A wave with `send DIR D` lands at its neighbor after `D` ticks and becomes
  actionable one tick later (`delay 0`).
* Collisions: the first arrival wins (acts as a merge/absorb primitive).
* Audio: each grid tick renders `sps` samples (default ≈735, ~60 ticks/sec).

Full model: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)

---

## Examples

| File                | Shows                                            |
|---------------------|--------------------------------------------------|
| `pulse.flux`        | wave train: generator drives 1-bits East         |
| `wave.flux`         | one wave tracing the matrix perimeter            |
| `serpent.flux`      | a bit drawing a snake across the whole grid      |
| `storm.flux`        | three waves, each leaving an accumulator trail   |
| `logic.flux`        | two streams meeting in an AND gate (acc=1 when both arrive) |
| `fm.flux`           | FM synthesis: 220 Hz carrier modulated by an LFO, frequency sequenced by waves |
| `arp.flux`          | chiptune arpeggio: 3 wave rows play notes on 3 channels |

---

## Layout

```
flux1.hpp           core types: Cell, Wave, Rule, Chan, class Flux
flux1.cpp           the tick, audio renderer, ASCII render
main.cpp            .flux parser, CLI, WAV writer
examples/*.flux     demo programs
docs/               language + architecture reference (EN & RU)
```

## License

MIT — do whatever you want with it.