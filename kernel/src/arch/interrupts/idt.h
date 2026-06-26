#include "arch/io.h"
#ifndef _IDT_H
#define _IDT_H 1

#include <stdint.h>

typedef struct idtr {
    uint16_t size;
    uint64_t offset;
} __attribute__((packed)) idtr_t;

typedef struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t flags;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed)) idt_entry_t;

extern const uint64_t isr_table[256];

void _lidt(void *idtr);

void idt_setup(void);
void idt_reload(void);

void stack_trace(int log_level, context_t *ctx);
void register_dump(int log_level, context_t *ctx);

#endif // _IDT_H
