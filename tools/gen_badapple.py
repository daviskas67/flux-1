#!/usr/bin/env python3
"""gen_badapple.py - generates a Bad-Apple-style 16x16 animation for Flux-1.

Produces:
  firmware/bad_apple.asm   assembler source
  firmware/bad_apple.flux  assembled ROM image

Animation loop:
  1. 4-bit frame counter in grid bits 0x00..0x03 (incremented each pass).
  2. A binary dispatch tree (on counter bits b3 b2 b1 b0) selects one of 16
     LEAF blocks.
  3. Each LEAF streams that frame's 32 bytes from the ROM frame table into
     the GRID via COPY.
  4. JMPF back to START.

Frames are generated procedurally (moving wave + orbiting ring + rotating
diagonal).

Usage: python tools/gen_badapple.py
Then:  asmflux firmware/bad_apple.asm -o firmware/bad_apple.flux
       flux firmware/bad_apple.flux --tps 30
"""
import math

W, H = 16, 16
TOTAL = 16  # unique frames (4-bit counter)


def frame_scanline(y, t):
    out = []
    for x in range(W):
        wave = 0.5 + 0.5 * math.sin((x + t * 0.6) * 1.4)
        cx = 7.5 + 4.0 * math.sin(t * 0.20)
        cy = 7.5 + 4.0 * math.cos(t * 0.15)
        d = math.hypot(x - cx, y - cy)
        ring = 1.0 if abs(d - 6.5) < 1.5 else 0.0
        diag = 1 if (x + y) % 5 == (t // 2) % 5 else 0
        on = 1 if (wave > 0.6 or ring > 0.5 or diag) else 0
        out.append('1' if on else '0')
    return ''.join(out)


def bits_to_bytes(lines):
    bs = [0] * 32
    for y, line in enumerate(lines):
        for x, ch in enumerate(line):
            if ch == '1':
                bs[(y * 16 + x) >> 3] |= 1 << ((y * 16 + x) & 7)
    return bs


def main():
    frames = [bits_to_bytes([frame_scanline(y, t) for y in range(H)])
              for t in range(TOTAL)]

    asm = []
    asm.append("; bad_apple.asm")
    asm.append("; Auto-generated Bad Apple-style 16x16 animation ({} frames).".format(TOTAL))
    asm.append("; A 4-bit counter in GRID bits 0..3 selects one of 16 frames;")
    asm.append("; each frame's 32 bytes are streamed into GRID via COPY.")
    asm.append("; Build: asmflux bad_apple.asm -o bad_apple.flux")
    asm.append("; Run:   flux bad_apple.flux --tps 30")
    asm.append("")
    asm.append("START:")
    asm.append("    SET  0x0F")            # permanent 1 cell (byte 0 preserved)
    asm.append("    ; ---- increment 4-bit counter (ripple) ----")
    for i in range(4):
        asm.append("    GET  0x%02X" % i)
        asm.append("    XOR  0x0F")            # 0x0F seeded = permanent 1
        asm.append("    PUT  0x%02X" % i)
        asm.append("    JZF  C%02d" % (i + 1))  # carry to next bit
        asm.append("    JMPF DISPATCH")
        asm.append("C%02d:" % (i + 1))
    asm.append("DISPATCH:")
    asm.append("    GET  0x03")
    asm.append("    JZF  D2_0")
    asm.append("    JMPF D2_1")
    asm.append("D2_0:")
    asm.append("    GET  0x02")
    asm.append("    JZF  D1_0")
    asm.append("    JMPF D1_1")
    asm.append("D2_1:")
    asm.append("    GET  0x02")
    asm.append("    JZF  D1_2")
    asm.append("    JMPF D1_3")
    asm.append("D1_0:")
    asm.append("    GET  0x01")
    asm.append("    JZF  D0_0")
    asm.append("    JMPF D0_1")
    asm.append("D1_1:")
    asm.append("    GET  0x01")
    asm.append("    JZF  D0_2")
    asm.append("    JMPF D0_3")
    asm.append("D1_2:")
    asm.append("    GET  0x01")
    asm.append("    JZF  D0_4")
    asm.append("    JMPF D0_5")
    asm.append("D1_3:")
    asm.append("    GET  0x01")
    asm.append("    JZF  D0_6")
    asm.append("    JMPF D0_7")
    for i in range(8):
        asm.append("D0_%d:" % i)
        asm.append("    GET  0x00")
        asm.append("    JZF  F%d" % (i * 2))
        asm.append("    JMPF F%d" % (i * 2 + 1))
    asm.append("")

    for i in range(TOTAL):
        asm.append("F%d:" % i)
        asm.append("    ; copy frame %d bytes 1..31 into GRID (byte 0 = counter)" % i)
        for j in range(1, 32):
            asm.append("    COPY %d FRAME_TABLE+%d" % (j, i * 32 + j))
        asm.append("    JMPF START")
    asm.append("")

    asm.append(".org 0x1000")
    asm.append("FRAME_TABLE:")
    for f in range(TOTAL):
        asm.append("    .byte " + ", ".join('0x%02X' % b for b in frames[f]))
    asm.append("")

    with open('firmware/bad_apple.asm', 'w', encoding='utf-8') as fh:
        fh.write('\n'.join(asm) + '\n')
    print('wrote firmware/bad_apple.asm ({} frames, {} bytes frame data @0x1000)'.format(
        TOTAL, TOTAL * 32))


if __name__ == '__main__':
    main()
