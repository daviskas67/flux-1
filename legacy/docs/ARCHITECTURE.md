# Flux-1 Architecture

This document describes the execution model of Flux-1: the cell, the wave, the
synchronous tick, collisions, and the audio pipeline. The language is specified
in [LANGUAGE.md](LANGUAGE.md).

---

## 1. The machine

Flux-1 is a **massively parallel, 1-bit, space-computing machine**. It is not a
von-Neumann CPU with registers and an instruction stream. It is a 16×16 grid of
**cells**, each of which is simultaneously a memory element, a tiny ALU and a
router. Computation happens *where the waves are*.

| Constant | Value | Meaning              |
|----------|-------|----------------------|
| `kGrid`  | 16    | grid edge (256 cells)|
| `kMaxDelay` | 15 | max transport delay |
| `kChans` | 3     | square audio channels|

---

## 2. The cell

```cpp
struct Cell {
    uint8_t bit : 1;   // data bit (wave presence)
    uint8_t acc : 1;   // accumulator bit (persistent flag)
    uint8_t delay;     // pending transport delay (ticks)
    Dir     dir;       // direction this node currently pushes its bit
};
```

* **`bit`** — whether a wave occupies the cell this tick.
* **`acc`** — a permanent 1-bit register. It is *not* cleared between ticks;
  it survives `memset` via a snapshot/restore in phase B. This is Flux-1's
  persistent memory. Trails, gates, flags all use `acc`.
* **`delay`** — remaining charging time for the current bit (see §4).
* **`dir`** — the cell's current routing direction, used by `send H`.

A cell also owns a **program**: an ordered list of `Rule`s (see LANGUAGE.md §8).

---

## 3. The wave

A wave is a bit in transit. Conceptually it is a tuple `(x, y, dir, bit, delay)`.
When a rule pushes a bit to a neighbor, a `Move` is scheduled with the target
coordinates and a **delay**; the wave "flies" for `delay` ticks before landing.

Delays are the programmer's timing tool: `send E 2` means the bit reaches the
East neighbor 2 ticks from now. Two waves with different delays can arrive at
different times even if sent on the same tick.

---

## 4. The synchronous tick

Flux-1 uses a strict **two-phase** model, like a real synchronous digital
circuit. All reads happen in phase A, all writes commit in phase B.

### Phase A — evaluate

For every cell `(x, y)`:

```
if (!cell.bit && cell has no rules) skip
hadBit = cell.bit

if (cell.bit && cell.delay > 0):
    cell.delay--            # charging
    stay.append((idx, cell.delay))
    continue

consumed = false
for each rule r in cell.rules (in order):
    if !condOk(r.cond, cell.bit, cell.acc): continue
    switch r.action:
        Send:   # only when cell.bit
                target = neighbor in (r.dir or cell.dir)
                if inbounds: moves.append(target, bit, r.delay)
                consumed = true
        Emit:   # generator: assert 1 to neighbor
                moves.append(target, 1, r.delay)
                # NOT consumed — keeps emitting
        Stop:   consumed = true
        SetAcc/ClearAcc/ToggleAcc: mutate cell.acc
        ChanFreq/ChanAmp/ChanPhase/ChanMod: mutate audio channel
    if consumed: break

if (!consumed && hadBit):
    stay.append((idx, 0))   # lingering wave
```

Key points:

* **First-match wins.** Rules are scanned in order; the first *consuming*
  match stops the scan. Non-consuming actions (acc ops, audio ops) run and the
  scan continues.
* **Rules run only with a bit ready to act** — a charging wave does not fire
  rules.
* **`emit` never consumes**, so emitters produce continuously.
* **Out-of-bounds `send` kills the wave** (no target scheduled).

### Phase B — commit

```
accSnap[i] = grid[i].acc                  # save accumulators
memset(grid, 0)                           # clear everything
grid[i].acc = accSnap[i]                  # restore accumulators

for (idx, d) in stay:   grid[idx].bit = 1; grid[idx].delay = d
for m in moves:         if grid[m.idx].bit: continue   # collision!
                        grid[m.idx].bit = 1
                        grid[m.idx].delay = m.delay
```

* All state changes are committed **simultaneously**. A cell may be read by a
  neighbor's rule and still send its own wave in the same tick — the two are
  independent.
* **Accumulators are preserved** across the tick (snapshot/restore).

---

## 5. Collisions

When two or more `Move`s land on the same cell in one tick, **the first one
wins** (iteration order: rows then columns, source-scan order). Later arrivals
are dropped. In practice well-formed programs give each cell a single rule and
rely on delay staggering, so collisions are rare; when they happen they act as
a *merge/absorb* primitive.

---

## 6. Timing math

A wave with `send DIR D`:

1. tick T — rule fires, `Move` scheduled for the neighbor.
2. ticks T+1 … T+D — the wave is **charging** at its destination
   (`delay` counts down; renders as `o`).
3. tick T+D+1 — the wave is actionable at the destination
   (`delay == 0`; renders as `#`).

So a delay of 2 means roughly 3 ticks per hop, and a 16-cell trip at
`send E 2` takes about 48 ticks. An `emit` with delay 0 places its bit one tick
later, giving a pulse train.

`inject ... D` behaves the same: the injected wave is actionable `D+1` ticks
after injection.

---

## 7. The audio pipeline

Sound is rendered **synchronously** with grid ticks. Every tick, `sps`
samples are produced (default `44100/60`). The grid's `freq`/`amp`/`phase`/`mod`
actions take effect at the *start* of the next sample block.

### Square oscillator

Each channel `c` is a phase accumulator:

```
dt   = sps / rate
twopi = 2π
for s in 0..sps-1:
    inst = c.freq
    if c.modSrc >= 0: inst += c.modDepth * channels[c.modSrc].last
    c.phase += inst * twopi * dt
    c.last  = c.amp * (sin(c.phase) >= 0 ? +1 : -1)
    mix += c.last
audio.push(mix)
```

* `inst` — instantaneous frequency; the FM term shifts it by
  `modDepth * sample(modulator)`.
* The square is derived from `sin(phase)` sign, giving a band-limited-ish
  binary waveform.
* `last` is the modulator's signal, so channel order in the `mod J D` routing
  matters for audio-rate FM stacks.

### WAV output

`--wav` writes a mono, 16-bit PCM file at `rate` Hz. Samples are hard-clamped
to `[-1, 1]` before conversion. Duration = `ticks * sps / rate` seconds.

---

## 8. Rendering

The ASCII render shows one grid frame:

| Glyph | Condition                            |
|-------|--------------------------------------|
| `.`   | `!bit && !acc`                       |
| `#`   | `bit && delay == 0`                  |
| `o`   | `bit && delay > 0`                   |
| `A`   | `!bit && acc` (only with `--acc`)    |

A footer `tick=N  waves=K` reports the tick and the number of live waves
(rebuilt at the end of each phase B).

---

## 9. Files

| File           | Role                                              |
|----------------|---------------------------------------------------|
| `flux1.hpp`    | types: `Cell`, `Wave`, `Rule`, `Chan`, class `Flux` |
| `flux1.cpp`    | the tick, audio renderer, ASCII render            |
| `main.cpp`     | `.flux` parser, CLI, WAV writer                   |
| `examples/*.flux` | demo programs                                    |

Compile:

```bash
g++ -O2 -std=c++17 main.cpp flux1.cpp -o flux1
```