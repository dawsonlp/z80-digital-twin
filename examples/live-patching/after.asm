; Insertion moves after_draw to $8004 and draw to $8012.
; At the paused draw entry: optionally change PC to $8012 and the saved
; return word to $8004. SP itself stays unchanged.
org $8000
entry:
    nop
    call draw
after_draw:
    halt
    defs $8012-$, 0
draw:
    nop
    ret
