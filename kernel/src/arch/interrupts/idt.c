#include <arch/interrupts/idt.h>

#include <arch/cpu.h>
#include <arch/interrupts/isr.h>
#include <arch/io.h>
#include <sched/sched.h>

#include <log/log.h>

#include <stddef.h>

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

void register_dump(int log_level, context_t *ctx) {
	log(log_level, "General Purpose Registers:\n");
	log(log_level, "  RAX=0x%016llx rbx=0x%016llx rcx=0x%016llx rdx=0x%016llx\n", ctx->rax, ctx->rbx, ctx->rcx, ctx->rdx);
	log(log_level, "  R8 =0x%016llx R9 =0x%016llx R10=0x%016llx R11=0x%016llx\n", ctx->r8, ctx->r9, ctx->r10, ctx->r11);
	log(log_level, "  R12=0x%016llx R13=0x%016llx R14=0x%016llx R15=0x%016llx\n", ctx->r12, ctx->r13, ctx->r14, ctx->r15);
	log(log_level, "segment registers:\n");
	log(log_level, "  CS=0x%04llx DS=0x%04llx ES=0x%04llx FS=0x%04llx GS=0x%04llx SS=0x%04llx\n", ctx->cs, ctx->ds, ctx->es, ctx->fs, ctx->gs, ctx->ss);
	log(log_level, "RFLAGS: 0x%016llx\n", ctx->rflags);
	log(log_level, "  CF=%d PF=%d AF=%d ZF=%d\n",
		(ctx->rflags & (1ULL << 0)) ? 1 : 0,
		(ctx->rflags & (1ULL << 2)) ? 1 : 0,
		(ctx->rflags & (1ULL << 4)) ? 1 : 0);
	log(log_level, "  SF=%d TF=%d IF=%d DF=%d\n",
		(ctx->rflags & (1ULL << 7)) ? 1 : 0,
		(ctx->rflags & (1ULL << 8)) ? 1 : 0,
		(ctx->rflags & (1ULL << 9)) ? 1 : 0,
		(ctx->rflags & (1ULL << 10)) ? 1 : 0);
	log(log_level, "  OF=%d IOPL=%d NT=%d RF=%d\n",
		(ctx->rflags & (1ULL << 11)) ? 1 : 0,
		(ctx->rflags >> 12) & 0x3,
		(ctx->rflags & (1ULL << 14)) ? 1 : 0,
		(ctx->rflags & (1ULL << 16)) ? 1 : 0);
	log(log_level, "  VM=%d AC=%d VIF=%d VIP=%d ID=%d\n",
		(ctx->rflags & (1ULL << 17)) ? 1 : 0,
		(ctx->rflags & (1ULL << 18)) ? 1 : 0,
		(ctx->rflags & (1ULL << 19)) ? 1 : 0,
		(ctx->rflags & (1ULL << 20)) ? 1 : 0,
		(ctx->rflags & (1ULL << 21)) ? 1 : 0);
	log(log_level, "Special Registers:\n");
	log(log_level, "  RIP=0x%016llx RSP=0x%016llx RBP=0x%016llx RDI=0x%016llx\n", ctx->rip, ctx->rsp, ctx->rbp, ctx->rdi);
	log(log_level, "  RSI=0x%016llx CR2=0x%016llx ERROR=0x%016llx VECTOR=0x%016llx\n", ctx->rsi, ctx->cr2, ctx->error, ctx->irq);
}

void stack_trace(int log_level, context_t *ctx) {
	uint64_t *rbp = (uint64_t *)ctx->rbp;

	log(LOG_LEVEL_CRITICAL, "rbp=%p\n", rbp);

	for (int i = 0; i < 4; i++) {
    	log(LOG_LEVEL_CRITICAL,
        	"rbp[%d] = %016llx\n",
        	i,
        	rbp[i]);
	}

	log(log_level, "Stack trace:\n");
	for (int i = 0; i < 16; i++) {
		if (rbp == NULL || (uintptr_t)rbp < 0x1000) {
			break;
		}
		uint64_t ret_addr = *(rbp + 1);
		log(log_level, "  #%d: 0x%016llx\n", i, ret_addr);
		rbp = (uint64_t *)(*rbp);
	}
}

static idt_entry_t idt[256];

void idt_setup(void) {
    for (int i = 0; i < 256; i++) {
        uint64_t addr = isr_table[i];
        idt[i].offset_low = addr & 0xFFFF;
        idt[i].selector = 0x8;
        idt[i].ist = 0; // TODO: maybe implement ist stuff in here
        if (i == 0x80) {
            idt[i].flags = 0xEE;
        } else {
            idt[i].flags = 0x8E;
        }
        idt[i].offset_mid = (addr >> 16) & 0xFFFF;
        idt[i].offset_high = (addr >> 32) & 0xFFFFFFFF;
    }
}

idtr_t idtr = {
	.size = (uint16_t)(sizeof(idt) - 1),
	.offset = (uint64_t)idt,
};

void exception_isr(isr_t* self, context_t *ctx) {
    _cli();
	if ((self->id & 0xff) < 19) {
		error("Exception %d: %s @ %p\n", self->id & 0xff, exceptions[self->id & 0xff], (void *)ctx->rip);
		register_dump(LOG_LEVEL_CRITICAL, ctx);
		stack_trace(LOG_LEVEL_CRITICAL, ctx);
		hcf();
	} else {
		error("Exception %d: Unknown @ %p\n", self->id & 0xff, (void *)ctx->rip);
		register_dump(LOG_LEVEL_CRITICAL, ctx);
		stack_trace(LOG_LEVEL_CRITICAL, ctx);
		hcf();
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
		critical("Unhandled interrupt %d\n", vec);
		register_dump(LOG_LEVEL_CRITICAL, ctx);
		stack_trace(LOG_LEVEL_CRITICAL, ctx);

		critical("Halting system.\n");
		hcf();
	}

	isr->handler(isr, ctx); // todo: priority bs

	if (isr->eoi) {
		isr->eoi(isr);
	}

	sched_check_resched();
}
