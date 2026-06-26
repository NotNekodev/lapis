bits 64

global usermode_test_entry
usermode_test_entry:
    xor rax, rax
.loop:
    inc rax
    pause
    jmp .loop
