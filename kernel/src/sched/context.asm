bits 64

global context_switch
context_switch:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    pushfq

    mov [rdi], rsp

    mov rsp, rsi

    popfq
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    ret

global thread_bootstrap_trampoline
thread_bootstrap_trampoline:
    mov rdi, r12
    call rbx
    extern thread_exit_current
    call thread_exit_current
    .hang:
    cli
    hlt
    jmp .hang

global fork_child_entry
fork_child_entry:
    mov rsp, rbx
    extern isr_resume_from_context
    jmp isr_resume_from_context
