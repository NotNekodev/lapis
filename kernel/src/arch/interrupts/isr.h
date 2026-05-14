#ifndef _ISR_H
#define _ISR_H

#include <arch/io.h>

typedef struct isr {
    struct isr *next; // linked list :o

    void (*handler)(struct isr* self, context_t *ctx);
    void (*eoi)(struct isr* self);

    void *private;
} isr_t;

#endif // _ISR_H