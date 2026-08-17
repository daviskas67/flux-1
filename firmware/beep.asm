; beep.asm
; A little three-channel chiptune using the FM/modulation engine.
; Channel 0: bass line   (square, freq set per note)
; Channel 1: lead melody (square)
; Channel 2: modulator, FM-driving channel 0's frequency (vibrato/clang)
;
; NOTE: JMP reaches only within the current 64-byte block (SPEC 2.3).
; The whole melody fits in one block, so a simple JUMP loop works.
; Built with: asmflux beep.asm -o beep.flux
; Play with:  flux beep.flux --wav beep.wav --secs 12

START:
    ; --- configure channel 2 as FM modulator of channel 0 ---
    ; MOD ch src depth dest : ch=2, src=ch0(0xF0), depth=200 (8.8), dest=freq(0)
    ;   depth 200/256 ~ 0.78 -> freq wobble of ~±0.78 Hz/... small; make it wide:
    ;   we'll use grid bit 0x00 (0) as an LFO instead via volume mod.
    ; Simplest demo: fixed 3-channel chord.
    DUTY 0 128
    VOL  0 0
    DUTY 1 128
    VOL  1 0
    DUTY 2 128
    VOL  2 0

    ; --- play C-major chord: C4 E4 G4 ---
    VOL  0 140
    FREQ 0 261
    VOL  1 110
    FREQ 1 329
    VOL  2 90
    FREQ 2 392

    ; sustain: loop back to START (JMPF = full 16-bit jump, works across blocks)
    JMPF START

; To hear melody, replace the chord above with a note table and a counter;
; see runner.asm for a GET/XOR/JZ pattern that branches on grid state.