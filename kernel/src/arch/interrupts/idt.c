#include "arch/io.h"
#include <arch/interrupts/idt.h>
#include <log/log.h>

static char *exceptions[] = {
	"Division by 0",
	"Debug",
	"NMI",
	"Breakpoint",
	"Overflow",
	"Bound Range Exceeded",
	"Invalid Opcode",
	"Device Not Available",
	"Double Fault",
	"Coprocessor Segment Overrun",
	"Invalid TSS",
	"Segment Not Present",
	"Stack-Segment Fault",
	"General Protection Fault",
	"Page Fault",
	"Unknown",
	"x87 Floating-Point Exception",
	"Alignment Check",
	"Machine Check",
	"SIMD Exception"
};

static idt_entry_t idt[256];

void idt_setup(void) {
    for (int i = 0; i < 256; i++) {
        uint64_t addr = isr_table[i];
        idt[i].offset_low = addr & 0xFFFF;
        idt[i].selector = 0x8;
        idt[i].ist = 0; // TODO: maybe implement ist stuff in here
        idt[i].flags = 0x8E;
        idt[i].offset_mid = (addr >> 16) & 0xFFFF;
        idt[i].offset_high = (addr >> 32) & 0xFFFFFFFF;
    }
}

void idt_reload(void) {
	idtr_t idtr = {
		.size = (uint16_t)(sizeof(idt) - 1),
		.offset = (uint64_t)idt,
	};

	_lidt(&idtr);

	_sti();
}

void interrupt_isr(int vec, context_t *ctx) {
	debug("Received interrupt: %d\n", vec);

	hcf();
}