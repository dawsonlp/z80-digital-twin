; Pause at draw after the CALL. The live stack contains after_draw ($8003).
org $8000
entry:
    call draw
after_draw:
    halt
    defs $8010-$, 0
draw:
    nop
    ret
