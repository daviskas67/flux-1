; music.asm
; A real chiptune: 2-bit note counter + three channels with modulation.
;
; Layout:
;   GRID[0x00], GRID[0x01]  = counter bits (low, high) -> picks 1 of 4 chords
;   GRID[0x0F]              = permanent 1 (used to XOR-to-toggle bits)
;   GRID[0x10]              = tempo beat flag (toggles every frame)
;
; Channels:
;   ch0 = bass  (square), ch1 = lead (square), ch2 = LFO FM-modulating ch0
;
; NOTE: base JMP stays within its own 64-byte block (SPEC 2.3), so this demo
; uses the two-byte extensions JMPF/JZF (full 16-bit jumps) to move anywhere.
;
; Build: asmflux music.asm -o music.flux
; Play:  flux music.flux --wav music.wav --secs 20 --tps 120

START:
    ; --- permanent 1 at 0x0F ---
    SET  0x0F

    ; --- channel 2 = LFO (6 Hz) FM-modulating channel 0's frequency ---
    DUTY 2 96
    VOL  2 0            ; ch2 is a modulator; keep it silent in the mix
    FREQ 2 6
    ; MOD ch src depth dest : ch0 freq += ch2.last * (140/256)
    MOD  0 0xF2 140 0
    ; ch1 volume modulated by the beat flag (grid 0x10), depth 120
    MOD  1 0x10 120 2

NEXT:
    ; --- ripple increment 2-bit counter ---
    GET  0x00
    XOR  0x0F            ; toggle low bit
    PUT  0x00
    JZF  CARRY           ; new low bit == 0  -> carry to high
    JMPF NOCARRY
CARRY:
    GET  0x01
    XOR  0x0F            ; toggle high
    PUT  0x01
NOCARRY:
    ; --- pick chord by counter (low bit, then high) ---
    GET  0x00
    JZF  LSB0
    GET  0x01
    JZF  CH2            ; (1,0)
    JMPF CH3            ; (1,1)
LSB0:
    GET  0x01
    JZF  CH0            ; (0,0)
    JMPF CH1            ; (0,1)

CH0:                    ; C major
    FREQ 0 131
    FREQ 1 262
    JMPF PLAY
CH1:                    ; A minor
    FREQ 0 110
    FREQ 1 220
    JMPF PLAY
CH2:                    ; F major
    FREQ 0 87
    FREQ 1 175
    JMPF PLAY
CH3:                    ; G major
    FREQ 0 98
    FREQ 1 196

PLAY:
    VOL  0 150
    VOL  1 120
    ; --- toggle beat flag ---
    GET  0x10
    XOR  0x0F
    PUT  0x10
    JMPF NEXT