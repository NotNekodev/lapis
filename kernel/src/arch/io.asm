bits 64

global _outb
_outb:
    mov dx, di
    mov al, sil
    out dx, al
    ret

global _hlt
_hlt:
    hlt
    ret

global _cli
_cli:
    cli
    ret

global _sti
_sti:
    sti
    ret