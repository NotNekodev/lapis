bits 64

global usermode_enter
usermode_enter:
    mov rax, rdi
    mov rcx, rsi

    cli

    mov rdi, 0x1b
    mov es, di
    mov ds, di

    push qword 0x1b
    push rcx
    push qword 0x202
    push qword 0x23
    push rax

    o64 iret
