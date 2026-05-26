#include <util/semaphore.h>

#include <arch/io.h>

void semaphore_init(semaphore_t *sem, int32_t count) {
    if (count < 0) {
        count = 0;
    }

    __atomic_store_n(&sem->value, count, __ATOMIC_RELEASE);
}

void semaphore_wait(semaphore_t *sem) {
    for (;;) {
        int32_t old = __atomic_load_n(&sem->value, __ATOMIC_ACQUIRE);

        while (old <= 0) {
            _pause();

            old = __atomic_load_n(&sem->value, __ATOMIC_ACQUIRE);
        }

        int32_t expected = old;
        int32_t desired = old - 1;

        if (__atomic_compare_exchange_n(&sem->value, &expected, desired, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            return;
        }
    }
}

void semaphore_signal(semaphore_t *sem) {
    __atomic_fetch_add(&sem->value, 1, __ATOMIC_RELEASE);
}

bool semaphore_try_wait(semaphore_t *sem) {
    int32_t old = __atomic_load_n(&sem->value, __ATOMIC_ACQUIRE);

    while (old > 0) {
        int32_t expected = old;
        int32_t desired = old - 1;

        if (__atomic_compare_exchange_n(&sem->value, &expected, desired, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            return true;
        }

        old = __atomic_load_n(&sem->value, __ATOMIC_ACQUIRE);
    }

    return false;
}