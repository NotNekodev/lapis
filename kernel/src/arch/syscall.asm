bits 64

extern generic_syscall_handler

global sysret_syscall_handler
global legacy_syscall_handler

%define SYSRET_USER_RSP_OFFSET 16
%define SYSRET_KERNEL_RSP_OFFSET 24

sysret_syscall_handler:
    mov [gs:SYSRET_USER_RSP_OFFSET], rsp
    mov rsp, [gs:SYSRET_KERNEL_RSP_OFFSET]

    push qword 0
    push qword 0
    push qword 0
    push qword 0
    push qword 0

    push r9
    push r8
    push r10
    push rdx
    push rsi
    push rdi
    push rax
    push rcx
    push r11

    mov rdi, rsp
    call generic_syscall_handler

    pop r11
    pop rcx
    add rsp, 8 ; skip rax because its the syscall return
    pop rdi
    pop rsi
    pop rdx
    pop r10
    pop r8
    pop r9

    add rsp, 40

    mov rsp, [gs:SYSRET_USER_RSP_OFFSET]

    o64 sysret

legacy_syscall_handler:
    cld
    cmp qword [rsp+24], 0x8
    je .notneeded1
    .notneeded1:
    push r9
    push r8
    push r10
    push rdx
    push rsi
    push rdi
    push rax
    push rcx
    push r11

    mov rdi, 0x10
	mov ds, rdi
	mov es, rdi

    mov rdi, rsp
	call generic_syscall_handler

	pop r11
	pop rcx
	add rsp, 8
	pop rdi
	pop rsi
	pop rdx
	pop r10
	pop r8
	pop r9

    cmp qword [rsp+8], 0x8
	je .notneeded2
	.notneeded2:
	o64 iret
