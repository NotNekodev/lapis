#ifndef _IO_H
#define _IO_H 1

#include <stdint.h>

void _hlt(void);
void _sti(void);
void _cli(void);
void _pause(void);

void _outb(uint16_t port, uint8_t value);

#define hcf() do { \
    _cli(); \
    _hlt(); \
} while (0)

typedef struct context {
    uint64_t cr2;
	uint64_t gs;
	uint64_t fs;
	uint64_t es;
	uint64_t ds;
	uint64_t rax;
	uint64_t rbx;
	uint64_t rcx;
	uint64_t rdx;
	uint64_t r8;
	uint64_t r9;
	uint64_t r10;
	uint64_t r11;
	uint64_t r12;
	uint64_t r13;
	uint64_t r14;
	uint64_t r15;
	uint64_t rdi;
	uint64_t rsi;
	uint64_t irq;
	uint64_t rbp;
	uint64_t error;
	uint64_t rip;
	uint64_t cs;
	uint64_t rflags;
	uint64_t rsp;
	uint64_t ss;
} __attribute__((packed)) context_t;

#endif // _IO_H