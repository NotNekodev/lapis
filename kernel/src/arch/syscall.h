#ifndef _SYSCALL_H
#define _SYSCALL_H 1

#include <stdint.h>

typedef struct syscall_frame {
    uint64_t r11;
    uint64_t rcx;
    uint64_t rax;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t r10;
    uint64_t r8;
    uint64_t r9;

   	uint64_t rip;
	uint64_t cs;
	uint64_t rflags;
	uint64_t rsp;
	uint64_t ss;
} __attribute__((packed)) syscall_frame_t;

void setup_syscall(void);

// TODO: global syscall table :thumbsup:

void sysret_syscall_handler(void);

#endif // _SYSCALL_H
