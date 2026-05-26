#ifndef _SEMAPHORE_H
#define _SEMAPHORE_H 1

#include <stdint.h>
#include <stdbool.h>

typedef struct semaphore {
    int32_t value;
} semaphore_t;

void semaphore_init(semaphore_t *sem, int32_t count);
void semaphore_wait(semaphore_t *sem);
void semaphore_signal(semaphore_t *sem);
bool semaphore_try_wait(semaphore_t *sem);

#endif // _SEMAPHORE_H