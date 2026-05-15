#ifndef _SPINLOCK_H
#define _SPINLOCK_H 1

#define SPINLOCK_INIT(name) {0, -1, name}

typedef struct spinlock {
    volatile int locked;
    volatile long owner; // cpu id 
    volatile char *name;
} spinlock_t;

void spinlock_acquire(spinlock_t *lock);
void spinlock_release(spinlock_t *lock);

#endif // _SPINLOCK_H