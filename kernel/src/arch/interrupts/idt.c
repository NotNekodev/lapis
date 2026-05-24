#include "arch/cpu.h"
#include "arch/interrupts/isr.h"
#include "arch/io.h"
#include "stddef.h"
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

idtr_t idtr = {
	.size = (uint16_t)(sizeof(idt) - 1),
	.offset = (uint64_t)idt,
};

void exception_isr(isr_t* self, context_t *ctx) {
	if ((self->id & 0xff) < 19) {
		error("Exception %d: %s @ %p\n", self->id & 0xff, exceptions[self->id & 0xff], (void *)ctx->rip);
	} else {
		error("Exception %d: Unknown @ %p\n", self->id & 0xff, (void *)ctx->rip);
	}
}

void idt_reload(void) {
	_lidt(&idtr);

	for (int i = 0; i < 0x20; i++) {
		register_interrupt(i, exception_isr, NULL);
	}	

	_sti();

	info("idt (%u): loaded IDT with %d entries, IDTR at 0x%.16llx\n", get_current_cpu()->id, sizeof(idt) / sizeof(idt[0]), (uint64_t)&idtr);
}

void interrupt_isr(int vec, context_t *ctx) {
	isr_t *isr = &get_current_cpu()->isr[vec];

	if (!isr->handler) {
		error("Unhandled interrupt %d\n", vec);
		hcf();
	}

	isr->handler(isr, ctx); // todo: priority bs

	if (isr->eoi) {
		isr->eoi(isr);
	}
}