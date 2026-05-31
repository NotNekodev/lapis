#include <arch/interrupts/isr.h>

#include <arch/cpu.h>

#include <stddef.h>

void register_interrupt(int vector, void (*func)(isr_t *self, context_t *ctx), void (*eoi)(isr_t *self)) {
    isr_t *isr = &get_current_cpu()->isr[vector];
    isr->handler = func;
    isr->eoi = eoi;
    isr->id = ((uint64_t)get_current_cpuid() << 32) | vector;
}

isr_t *allocate_interrupt(void (*func)(isr_t *self, context_t *ctx), void (*eoi)(isr_t *self)) {
    isr_t *isr = NULL;

	for (int i = 0; i < 256; ++i) {
		if (get_current_cpu()->isr[i].handler == NULL) {
			isr = &get_current_cpu()->isr[i];
			register_interrupt(i, func, eoi);
			break;
		}
	}

	return isr;
}

void unregister_interrupt(int vector) {
	isr_t *isr = &get_current_cpu()->isr[vector];
	isr->handler = NULL;
	isr->eoi = NULL;
	isr->id = 0;
}