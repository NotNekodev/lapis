#ifndef _ISR_H
#define _ISR_H 1

#include <arch/io.h>

typedef struct isr {
    struct isr *next; // linked list :o

    void (*handler)(struct isr* self, context_t *ctx);
    void (*eoi)(struct isr* self);

    long id; // unique id

    void *private;
} isr_t;

void    register_interrupt(int vector, void (*func)(isr_t *self, context_t *ctx), void (*eoi)(isr_t *self));
isr_t  *allocate_interrupt(void (*func)(isr_t *self, context_t *ctx), void (*eoi)(isr_t *self));

#endif // _ISR_H