// this is only for bootstrapping the LAPIC timer, nothing more

#ifndef _PIT_H
#define _PIT_H 1

#include <stdint.h>
#include <stdbool.h>

#define PIT_FREQUENCY 1193182

void pit_init(uint32_t hz);
void pit_stop(void);

uint64_t pit_get_ticks(void);

void pit_sleep(uint64_t ticks);

void pit_irq_handler(void);

#endif // _PIT_H