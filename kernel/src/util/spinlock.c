#include <util/spinlock.h>

#include <arch/cpu.h>
#include <log/log.h>

void spinlock_acquire(spinlock_t *lock) {
    if (lock->irq_lock) {
        lock->rflags = _get_rflags();
        _cli();
    }
    long me = get_current_cpuid();

    if (lock->locked && lock->owner == me) {
        // you did something wrong
        critical("spinlock %s encountered a recursive deadlock on CPU %u", lock->name ? lock->name : "(no name)", me);
    }

    for (;;) {
        int expected = 0;

        if (__atomic_compare_exchange_n(
            &lock->locked,
            &expected,
            1,
            0,
            __ATOMIC_ACQUIRE,
            __ATOMIC_RELAXED
        )) {
            lock->owner = me;
            return;
        }

        _pause();
    }

}

void spinlock_release(spinlock_t *lock) {
    long me = get_current_cpuid();

    if (lock->owner != me) {
        critical("spinlock %s encountered a release from CPU %u, but it is owned by CPU %u", lock->name ? lock->name : "(no name)", me, lock->owner);
    }

    lock->owner = -1;

    __atomic_store_n(&lock->locked, 0, __ATOMIC_RELEASE);

    if (lock->irq_lock) {
        _set_rflags(lock->rflags);
    }
}
