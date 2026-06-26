#ifndef _WAITQUEUE_H
#define _WAITQUEUE_H 1

#include <stdint.h>
#include <util/spinlock.h>
#include <stdbool.h>

typedef struct thread thread_t;

typedef struct wait_node {
    struct wait_node *next;
    struct wait_node *prev;
    thread_t *thread;
    bool woken;
} wait_node_t;

typedef struct waitqueue {
    spinlock_t lock;
    wait_node_t *head;
    wait_node_t *tail;
} waitqueue_t;

#define WAITQUEUE_INIT(name) { SPINLOCK_INIT(name), NULL, NULL }

void waitqueue_init(waitqueue_t *wq, const char *name);
void waitqueue_wait(waitqueue_t *wq);
int waitqueue_wait_timeout(waitqueue_t *wq, uint64_t timeout_ms);
void waitqueue_wake_one(waitqueue_t *wq);
void waitqueue_wake_all(waitqueue_t *wq);
bool waitqueue_has_waiters(waitqueue_t *wq);

#endif // _WAITQUEUE_H
