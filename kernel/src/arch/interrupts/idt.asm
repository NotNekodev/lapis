bits 64

%assign i 0
%rep 256

extern dump_iret_frame
extern dump_current_thread_kstack

isr_%+ i:
    %if i <> 8 && i <> 10 && i <> 11 && i <> 12 && i <> 13 && i <> 14
        push qword 0
    %endif

    push rbp
    mov rbp, i

    jmp isr_common
%assign i i + 1
%endrep

isr_common:
    cmp qword [rsp+24], 0x8
    je .notneeded1
    swapgs
    .notneeded1:
    push rbp ; irq num, remember :^)
    push rsi
    push rdi
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rdx
    push rcx
    push rbx
    push rax
    mov rax, ds
    push rax
	mov rax, es
	push rax
	mov rax, fs
	push rax
	mov rax, gs
	push rax
	mov rdi, cr2
	push rdi

    mov rdi, 0x10
	mov ds, rdi
	mov es, rdi

    mov rdi, rbp
    mov rsi, rsp

	mov rbp, rsp

    cld ; yes sasdallas, i did it :face_holding_back_tears:
	extern interrupt_isr
	call interrupt_isr
	cli
	jmp isr_resume_from_context

global isr_resume_from_context
isr_resume_from_context:
    add rsp, 24 ; remove cr2, gs and fs
    pop rax
	mov es, rax
	pop rax
	mov ds, rax
	pop rax
	pop rbx
	pop rcx
	pop rdx
	pop r8
	pop r9
	pop r10
	pop r11
	pop r12
	pop r13
	pop r14
	pop r15
	pop rdi
	pop rsi
	add rsp, 8 ; remove irq
	pop rbp
	add rsp, 8 ; remove error code

    cmp qword [rsp+8], 0x8
	je .notneeded2
	swapgs
	.notneeded2:
	o64 iret

global _lidt
_lidt:
	lidt [rdi]
	ret

section .rodata
global isr_table
isr_table:
	%assign i 0
	%rep 256
	dq isr_%+ i
	%assign i i + 1
	%endrep
