#include "arch/io.h"
#ifndef _CPU_H
#define _CPU_H 1

#include <arch/interrupts/isr.h>
#include <stdint.h>
#include <uacpi/acpi.h>

// TODO: do something with this qwq
typedef struct ist {
	uint32_t reserved;
	uint64_t rsp0;
	uint64_t rsp1;
	uint64_t rsp2;
	uint64_t reserved2;
	uint64_t ist1;
	uint64_t ist2;
	uint64_t ist3;
	uint64_t ist4;
	uint64_t ist5;
	uint64_t ist6;
	uint64_t ist7;
	uint32_t reserved3[3];
	uint32_t iopb;
} __attribute__((packed)) ist_t;

typedef struct cpu {
	struct cpu *self;
	uint32_t id;
	uint32_t lapic_id; // they should not be different, but they can be? idk qwq

	uint64_t sysret_user_rsp;
	uint64_t sysret_kernel_rsp;

	uint64_t gdt[5];
	ist_t tss;
	isr_t isr[256]; // max amount of interrupts on x86_64, WHICH IS WHAT WE ARE TARGETING!!

	struct runqueue *rq;
	struct thread *idle_thread;
	struct thread *current_thread;
	struct thread *zombie_to_reap;
	volatile uint8_t need_resched;
	volatile uint8_t in_sched;
} cpu_t;

#define _cpuid(leaf, subleaf, a, b, c, d) \
	__asm__ volatile ("cpuid" \
					  : "=a"(a), "=b"(b), "=c"(c), "=d"(d) \
					  : "a"(leaf), "c"(subleaf))

cpu_t *get_bsp(void);

void cpu_set_current(cpu_t *cpu);
cpu_t *get_current_cpu(void);
long get_current_cpuid(void);

uint64_t tsc_get_nanoseconds(void);

#endif // _CPU_H
