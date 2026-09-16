; Change color from red to green, then run Spectrum: Build and Run.
    org $8000
    include "palette.inc"
color1 equ red
color2 equ green
color3 equ blue

; Input:    None; requires a valid stack for subroutine calls.
; Clobbers: A, BC, DE, HL, flags; disables interrupts. Does not return.
start:
    di
    ld a,color1
    call setcolor
    ld a, $f0
    call delay
    ld a, color2
    call setcolor
    ld a, $f0
    call delay
    ld a, color3
    call setcolor
    jp idle ; and fall through to idle - well let's be explicit

; Input:    None.
; Clobbers: None; loops forever without returning.
idle:
    jr idle

; Input:    A = Spectrum color, 0..7.
; Clobbers: A, BC, DE, HL, flags; writes the border and screen attributes.
setcolor:
    out ($fe),a
    ld hl,$5800
    ld de,$5801
    ld bc,767
    sla a ;shifted a 3 places left into the paper color position
    sla a
    sla a
    ld (hl), a 
    ldir
    ret


; Input:    A = delay count, 1..255 (0 gives 256 outer iterations).
; Clobbers: A, B, flags. Returns with A = 0 and B = 0.
delay:
    ld b, $ff
; Internal loop: consumes B; part of delay, not a separate callable routine.
inner_delay:
    dec b
    jr nz, inner_delay
    dec a
    jr nz, delay
    ret

