; Stable real-assembler acceptance input; the example is a user scratchpad.
    org $8000
    include "palette.inc"
color equ red
start:
    di
    ld a,color
    out ($fe),a
    ld hl,$5800
    ld de,$5801
    ld bc,767
    ld (hl),color*8
    ldir
idle:
    jp idle
