#include <stdint.h>
#ifndef _SPINLOCK_H
#define _SPINLOCK_H 1

#define SPINLOCK_INIT(name) {0, -1, name, 0, 0}
#define SPINLOCK_IRQ_INIT(name) {0, -1, name, 1, 0}

typedef struct spinlock {
    volatile int locked;
    volatile long owner; // cpu id
    volatile char *name;

    int irq_lock; // if this spinlock should also be an IRQ lock
    uint64_t rflags;
} spinlock_t;

void spinlock_acquire(spinlock_t *lock);
void spinlock_release(spinlock_t *lock);

#endif // _SPINLOCK_H
