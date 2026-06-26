bits 64

global _lgdt
global _ltr
global _reload_segs

_lgdt:
    lgdt [rdi]
    ret

_ltr:
    mov ax, di
    ltr ax
    ret

_reload_segs:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    push 0x08
    push .reload
    retfq
.reload:
    ret
