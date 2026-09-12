org $8000

start:
    ld a,2Ah
    ld hl,message
    jp print

print:
    ret

message:
    db "hello from lsp_z80",0 ; comments remain visible in hover previews
    db 13,10
    db 0
    db "not shown in the three-line preview"
