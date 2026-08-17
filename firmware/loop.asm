; loop.asm
; Running light (SPEC.md 6): blink/step across the top-left corner.
; Assembled with: asmflux loop.asm -o loop.flux
; Run with:       flux loop.flux --tps 30

START:
    SET  0x00          ; light cell 0
    CLR  0x01          ; clear cell 1
    JMP  START         ; loop forever

; each pass: SET 0x00, CLR 0x01 — the light hops between two cells.