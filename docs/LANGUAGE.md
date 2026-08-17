# ASM-Flux Language Reference

ASM-Flux is the assembly language for the FLUX-1 1-bit computer. A program is
compiled with `asmflux` into a raw binary ROM image (`.flux`) that the
emulator loads.

```
usage: asmflux in.asm [-o out.flux] [-l list.txt]
```

## Syntax

* One instruction per line; `;` and `#` start a comment (to end of line).
* Numbers: decimal, or `0x`/`0b` prefixed (strtol base-0).
* Labels: `NAME:` — defined at the current address.
* Label arithmetic in operands: `NAME+16`, `NAME-2` (used with `COPY`, `.byte`,
  `.word`, jumps).
* Case-insensitive mnemonics.

## Base instructions (1 byte)

| bits 7..6 | mnemonic | opcode | meaning                              |
|-----------|----------|--------|--------------------------------------|
| `00`      | `NOP`    | `0x00` | no operation                         |
| `01`      | `SET a`  | `0x40\|a` | grid[a] = 1                       |
| `10`      | `CLR a`  | `0x80\|a` | grid[a] = 0                       |
| `11`      | `JMP a`  | `0xC0\|a` | PC = (PC & 0xFFC0) \| a           |

`a` is 6 bits (0..63): SET/CLR address the grid (0..255), JMP stays within the
current 64-byte block.

## Extensions (0xFC prefix)

The byte `0xFC` is never produced by the base instructions, so it acts as an
escape: `0xFC <sub> <operands>`.

| mnemonic | sub | encoding       | cycles* | meaning                       |
|----------|-----|----------------|---------|-------------------------------|
| `GET a`  | 0x00| `FC 00 a`      | +1      | ACC = grid[a]                 |
| `PUT a`  | 0x01| `FC 01 a`      | +1      | grid[a] = ACC                 |
| `XOR a`  | 0x02| `FC 02 a`      | +1      | ACC ^= grid[a]                |
| `JZ a`   | 0x03| `FC 03 a`      | +1      | if ACC==0: block-local jump   |
| `JMPF a` | 0x05| `FC 05 aL aH`  | +2      | PC = a (16-bit)               |
| `JZF a`  | 0x06| `FC 06 aL aH`  | +2      | if ACC==0: PC = a             |
| `COPY d s`| 0x07| `FC 07 d sL sH`| +3      | grid[d] = ROM[s]              |
| `SETCH c p v` | 0x04 | `FC 04 c p vL vH` | +2 | channel parameter c,p = v |

\* extra cycles beyond the base fetch/execute; cycles matter for timing audio.

## Audio (`SETCH c p v`)

Channels `c` = 0,1,2. Parameters:

| p | param       | width  | meaning                                        |
|---|-------------|--------|------------------------------------------------|
| 0 | `freq`      | 16-bit | frequency in Hz                                |
| 1 | `duty`      | 16-bit | pulse width (low 8 bits used; 0 = silent)      |
| 2 | `volume`    | 16-bit | 0..255 (low 8 bits used)                       |
| 3 | `mod_src`   | 16-bit | 0x00..0xFF grid bit, 0xF0..0xF2 channel, 0xFF off |
| 4 | `mod_depth` | 16-bit | 8.8 fixed point (256 = 1.0, signed)            |
| 5 | `mod_dest`  | 16-bit | 0=freq 1=duty 2=volume                          |

Assembler sugar:

```
FREQ ch v
DUTY ch v
VOL  ch v
MOD  ch src depth dest     ; expands to 3 SETCH instructions (18 bytes)
```

### Modulation algorithm

Per sample, for each channel:

```
base   = channel.freq (or duty, or volume) depending on mod_dest
src    = grid bit value (0/1), or channel output (+1/−1), or 0 if mod off
target = base + src * mod_depth / 256
```

* `mod_src` in `0x00..0xFF`: reads that GRID bit. Depth 256 × bit 1 = +1.0.
* `mod_src` in `0xF0..0xF2`: source is that channel's square output (−1/+1).
  Negative depth inverts it. This gives FM (dest=freq), pulse-width modulation
  (dest=duty), tremolo (dest=volume).
* `mod_src = 0xFF`: modulation off.

### The square wave

Each channel holds a 16-bit phase accumulator advanced per sample:

```
phase += freq * 65536 / sample_rate      (modulo 65536)
sample = (phase < duty * 256) ? +1 : -1   (duty in 0..255)
```

Channels sum and clip to 16-bit PCM (`-1.0..+1.0 → -32767..+32767`).

## Directives

| directive        | meaning                                        |
|------------------|------------------------------------------------|
| `.org N`         | continue assembling at address N               |
| `.byte v,...`    | emit bytes (comma-separated, label arithmetic) |
| `.word v,...`    | emit 16-bit words, little-endian               |
| `.incbin "file"` | include a raw binary file                      |
| `;` / `#`        | comment to end of line                         |

## Examples

Running light:

```asm
START:
    SET  0x00
    CLR  0x01
    JMP  START          ; 64-byte block, loop forever
```

Counter cell (toggle) using a permanent-1 cell at 0x0F:

```asm
    SET  0x0F           ; permanent 1
    GET  0x00
    XOR  0x0F           ; ACC = bit0 ^ 1  (toggle)
    PUT  0x00           ; bit0 flipped
    JZF  CARRY          ; if new bit0==0 there was a carry
    JMPF DONE
CARRY:
    GET  0x01
    XOR  0x0F
    PUT  0x01
DONE:
```

Streaming a ROM frame into GRID (Bad Apple style):

```asm
    COPY 0  FRAME+0     ; GRID byte 0 = ROM byte at FRAME
    COPY 1  FRAME+1
    ...
FRAME:
    .byte 0x0F, 0xF0, ...
```

## Notes

* `0xFC` cannot be written as a data byte by SET/CLR (addresses are ≤ 0xFF but
  SET/CLR encode `0x40..0x7F`/`0x80..0xBF`); use `.byte 0xFC` in data if ever
  needed — the CPU will decode it as an extension.
* Blocks: `JMP`/`JZ` are limited to the current 64-byte block by design; use
  `JMPF`/`JZF` (4 bytes) to jump anywhere.