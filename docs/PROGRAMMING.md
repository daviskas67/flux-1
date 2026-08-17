# Programming FLUX-1

This guide teaches you how to actually write programs for the FLUX-1. It is
hardware-first: the machine is a 1-bit CPU whose only memory is the GRID, so
every program is about shuffling single bits, building logic out of branches,
and treating the ROM as firmware. Sound is just one peripheral among others.

Read [LANGUAGE.md](LANGUAGE.md) for the full instruction reference and
[ARCHITECTURE.md](ARCHITECTURE.md) for the machine model. This guide is about
*technique*.

## 1. The mental model

```
ROM (64 KB) ──► CPU ──► GRID (256 bits = 32 bytes) ──► screen (16×16)
                  │
                  └──► 3 square channels (audio, optional)
```

* There are **no bytes to compute with**. The only data type is a single bit.
* The only "registers" are `ACC` (1 bit) and `PC` (16 bits).
* `SET`/`CLR` write a bit. `GET` reads a bit into `ACC`. `XOR` flips `ACC`
  against a bit. `PUT` writes `ACC` into a bit.
* There is **no AND, no OR, no ADD, no subtract**. You build all of that from
  `XOR` and conditional jumps. That is the entire game.

Think of the GRID as a breadboard: each of the 256 positions is a node you can
set high (`SET`), pull low (`CLR`), read (`GET`), or toggle (`XOR`). Programs
are wiring diagrams in text form.

## 2. Hello: a running light

```asm
START:
    SET  0x00        ; light the first pixel
    CLR  0x01        ; extinguish the second
    JMP  START       ; loop (within the 64-byte block)
```

Assemble and run:

```
asmflux firmware/loop.asm -o firmware/loop.flux
flux firmware/loop.flux --tps 4
```

`JMP` only reaches the current 64-byte block (see the spec). Use `JMPF` when
your program grows past one block.

## 3. The flip-flop: one bit of state that changes

You cannot read and write in one instruction, and there is no "invert". The
standard trick is a **permanent 1 cell**:

```asm
SET  0x0F           ; a bit nobody ever clears
GET  0x00           ; ACC = grid[0x00]
XOR  0x0F           ; ACC = grid[0x00] XOR 1  -> inverted
PUT  0x00           ; grid[0x00] = ACC  (now the opposite value)
```

Every pass through this snippet, bit 0x00 flips. That is a D-latch behavior —
the basis for every counter below.

## 4. A ripple counter (hardware-grade)

A ripple carry counter increments one bit; if the bit wraps 1→0 it "carries"
into the next bit, like real binary adders. `firmware/counter.asm` builds an
8-bit counter from 8 flip-flops:

```asm
START:
    SET  0x0F            ; permanent 1
    ; bit 0
    GET  0x00
    XOR  0x0F            ; ACC = bit0 XOR 1
    PUT  0x00
    JZF  C01             ; new bit0 == 0 -> carry into bit1
    JMPF START           ; else done for this tick
C01:
    GET  0x01
    XOR  0x0F
    PUT  0x01
    JZF  C02
    JMPF START
    ; ... and so on up to bit 7
```

Watch it with `flux firmware/counter.flux --tps 30 --acc`. The 8 bits are row
0 of the display. Note the **asymmetric loop length**: a pass that only flips
bit 0 is 5 instructions, a pass that flips all 8 is 37. This asymmetry is real
hardware behavior; use it or account for it when timing things.

## 5. Arithmetic in software: the ripple-carry full adder

FLUX-1 has no ADD. `firmware/adder.asm` implements addition bit by bit using a
full-adder *circuit in software*. For each stage:

```
S_i    = A_i XOR B_i XOR C_in
C_out  = majority(A_i, B_i, C_in)
       = (A_i AND B_i) OR (A_i AND C_in) OR (B_i AND C_in)
```

`AND`/`OR` are implemented as **branch trees** (the CPU acting as logic gates):

```asm
    ; C_out = majority(A,B,C)  (registers A, B, carry cell C)
    GET  A
    JZF  caseA0
    ; A=1 -> C_out = B OR C
    GET  B
    JZF  caseA1B0
    SET  CARRY            ; A=1,B=1 -> 1
    JMPF next
caseA1B0:                 ; A=1,B=0 -> C_out = C (leave CARRY alone)
    JMPF next
caseA0:                   ; A=0 -> C_out = B AND C
    GET  B
    JZF  caseA0B0
    JMPF next             ; A=0,B=1 -> C_out = C
caseA0B0:                 ; A=0,B=0 -> 0
    CLR  CARRY
next:
    ; S = A XOR B XOR C
    GET  A
    XOR  B
    XOR  CARRY
    PUT  S
```

The generated `firmware/adder.asm` chains four such stages (A at 0x00..0x03,
B at 0x04..0x07, carry at 0x08, result S at 0x09..0x0D). Regenerate any test
vector and check it:

```
python tools/gen_adder.py 13 7
asmflux firmware/adder.asm -o firmware/adder.flux
flux firmware/adder.flux --grid-hex --steps 300
grid hex: 7F 40 ...        ; byte0 = A|B, byte1: S = 0b10100 = 20  (13+7)
```

The whole "ALU" is ~50 instructions of branch trees. That is what "1-bit
computer" means: every useful operation costs you instruction space.

## 6. Data movement and ROM tables

`COPY dst src16` streams a ROM byte into a GRID byte (32 bits at a time for a
whole row). This is how firmware pushes data/frames/constants into the state
machine:

```asm
    COPY 0  FRAME+0     ; GRID byte 0 = ROM byte at FRAME+0
    COPY 1  FRAME+1
    ...
FRAME:
    .byte 0x3C, 0x66, 0x0F, ...
```

Use `.org` to place big tables outside the code region, and label arithmetic
to index them:

```asm
    COPY 4  SINE+16     ; GRID byte 4 = SINE[16]
```

For *bit* data, `GET`/`PUT` with computed addresses do the job, but remember
`SET`/`CLR`/`GET`/`PUT` address bits 0..255 directly (the low 6 bits of the
instruction, bits 5..0).

## 7. Control flow patterns

### Finite state machine

The classic FLUX-1 idiom. A state bit (or counter) lives in the GRID; the
program reads it and jumps:

```asm
    GET  STATE
    JZF  S0
    JMPF S1
S0:
    ... state 0 code ...
    JMPF LOOP
S1:
    ... state 1 code ...
LOOP:
    ...
```

For more than 2 states, use a small counter and a dispatch tree (test the most
significant bit first, then the next — a binary search on bits).

### Subroutine-ish behavior

There is no CALL/RET. The usual approach:

1. **Inline** small helpers (paste the code where needed).
2. For repeated code, use a **jump table in ROM**: store target addresses as
   `.word` and pick with a counter:

```asm
    COPY TEMP TABLE_IDX    ; read index from GRID, but the table is ROM...
```

Since addresses are 16-bit and live in ROM, the cleanest table dispatch is to
load the address via `COPY` into GRID bytes and then… but you can't jump to a
runtime-computed address — `JMPF` takes a fixed operand. So **tables of code
addresses are not jumpable at runtime**. Instead, dispatch with a chain of
`GET`/`JZF`/`JMPF` (a binary tree). This is the honest 1-bit way.

## 8. Debugging

The emulator is your logic analyzer:

```
flux prog.flux --steps 20 --grid-hex    ; run 20 instructions, dump GRID bytes
flux prog.flux --steps 20 --acc         ; also show channel registers
flux prog.flux --tps 4                  ; watch it run at 4 ticks/sec
```

`--grid-hex` prints all 32 GRID bytes as hex — this is how you verify the
adder above. `--steps` lets you advance the machine deterministically and
inspect state at a chosen point.

### Convention for a clean layout

Because `SET`/`CLR` only address bits 0..255 (7-bit-ish but masked to 6 bits
for JMP), keep your state in low addresses and tables in high ROM addresses:

| region | use |
|--------|-----|
| GRID 0x00..0x0F | working bits, counters, flags (row 0 on screen) |
| GRID 0x10..0xFF | data, arrays, bitmaps |
| ROM 0x0000..     | code |
| ROM 0x3000..     | data tables (`.org`) |

## 9. Audio as a peripheral

Audio is *not* the point of the machine, but it exists and it is driven the
same way as everything else: by writing bits (channel parameters) with
`SETCH`. Three square channels with frequency/duty/volume/modulation. See
[LANGUAGE.md](LANGUAGE.md) for `FREQ`/`DUTY`/`VOL`/`MOD`. The important part:
**the same loop that computes and draws also plays sound** — the channels are
polled every sample, so a melody is just a side effect of your main loop.

## 10. Exercises

1. Modify `counter.asm` to count only 0..3 (stop the ripple at bit 1).
2. Write a program that lights a 4×4 square and shifts it right one column
   every pass (use COPY + a row bitmap in ROM).
3. Extend `gen_adder.py` to 8-bit operands (A at 0x00..0x07 etc.).
4. Implement `A AND B` and `A OR B` as branch-tree subroutines (inline) and
   use them to make `majority` a 3-line expression.
5. Build a 2-state sequencer (square wave on a grid bit) and route it to a
   channel's volume via `MOD` — blink the screen in time with the beep.