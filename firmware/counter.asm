; counter.asm
; 8-bit binary counter built from flip-flops on the GRID.
;
; This is the classic "hardware" demo: the GRID itself is a register.
; Grid bits 0x00..0x07 are the counter (LSB = bit 0). Grid bit 0x0F is a
; "permanent 1" cell used with XOR to flip a bit:
;
;     GET b ; ACC = grid[b]
;     XOR 1 ; ACC = b XOR 1  -> toggles
;     PUT b ; grid[b] = ACC
;
; A ripple-carry counter: increment bit 0; if it wraps to 0, carry into bit 1;
; if bit 1 wraps, carry into bit 2, and so on. When the LSB does NOT wrap, we
; are done and jump straight back to START.
;
; Built with: asmflux counter.asm -o counter.flux
; Watch with: flux counter.flux --tps 30
; (the 8 counter bits are row 0, columns 0..7; bit 0x0F is the permanent 1)
;
; Layout:
;   0x00..0x07  counter bits (LSB..MSB)
;   0x0F        permanent 1 (never cleared) - the flip-flop clock source

START:
    SET  0x0F            ; permanent 1 cell
    ; ---- ripple increment: bit 0 ----
    GET  0x00
    XOR  0x0F            ; ACC = bit0 XOR 1
    PUT  0x00            ; bit0 = new value (flipped)
    JZF  C01             ; if new bit0 == 0 -> carry into bit 1
    JMPF START           ; else no carry, start over
C01:
    GET  0x01
    XOR  0x0F
    PUT  0x01
    JZF  C02
    JMPF START
C02:
    GET  0x02
    XOR  0x0F
    PUT  0x02
    JZF  C03
    JMPF START
C03:
    GET  0x03
    XOR  0x0F
    PUT  0x03
    JZF  C04
    JMPF START
C04:
    GET  0x04
    XOR  0x0F
    PUT  0x04
    JZF  C05
    JMPF START
C05:
    GET  0x05
    XOR  0x0F
    PUT  0x05
    JZF  C06
    JMPF START
C06:
    GET  0x06
    XOR  0x0F
    PUT  0x06
    JZF  C07
    JMPF START
C07:
    GET  0x07
    XOR  0x0F
    PUT  0x07            ; MSB wrapped too -> counter wrapped around 0xFF..0x00
    JMPF START