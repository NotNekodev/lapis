bits 64

global _outb
_outb:
    mov dx, di
    mov al, sil
    out dx, al
    ret

global _outw
_outw:
    mov dx, di
    mov ax, si
    out dx, ax
    ret

global _outd
_outd:
    mov dx, di
    mov eax, esi
    out dx, eax
    ret

global _inb
_inb:
    mov dx, di
    in al, dx
    movzx rax, al
    ret

global _inw
_inw:
    mov dx, di
    in ax, dx
    movzx rax, ax
    ret

global _ind
_ind:
    mov dx, di
    in eax, dx
    mov rax, rax
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

global _pause
_pause:
    pause
    ret

global _get_rflags
_get_rflags:
    xor rax, rax

    pushfq
    pop rax

    ret

global _set_rflags
_set_rflags:
    push rdi

    popfq

    ret

global _rdmsr
_rdmsr:
    mov ecx, edi
    rdmsr
    mov rax, rax
    ret

global _wrmsr
_wrmsr:
    mov ecx, edi
    mov eax, esi
    mov rdx, rsi
    shr rdx, 32
    wrmsr
    ret

global _rdtsc
_rdtsc:
    lfence
    rdtsc
    shl rdx, 32
    or rax, rdx
    ret
