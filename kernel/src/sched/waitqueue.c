#include <sched/waitqueue.h>
#include <sched/task.h>
#include <sched/sched.h>
#include <arch/cpu.h>
#include <arch/io.h>
#include <log/log.h>
#include <stddef.h>

void waitqueue_init(waitqueue_t *wq, const char *name) {
    wq->lock = (spinlock_t)SPINLOCK_INIT((char *)name);
    wq->head = NULL;
    wq->tail = NULL;
}

static void wq_link(waitqueue_t *wq, wait_node_t *node) {
    node->next = NULL;
    node->prev = wq->tail;
    node->woken = false;

    if (wq->tail) {
        wq->tail->next = node;
    } else {
        wq->head = node;
    }

    wq->tail = node;
}

static void wq_unlink(waitqueue_t *wq, wait_node_t *node) {
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        wq->head = node->next;
    }

    if (node->next) {
        node->next->prev = node->prev;
    } else {
        wq->tail = node->prev;
    }

    node->next = NULL;
    node->prev = NULL;
}

void waitqueue_wait(waitqueue_t *wq) {
    thread_t *self = thread_current();

    _cli();
    spinlock_acquire(&wq->lock);

    self->wait_node.thread = self;
    self->wait_node.woken = false;
    wq_link(wq, &self->wait_node);
    self->blocked_on = wq;

    sched_mark_blocked(self);

    spinlock_release(&wq->lock);

    schedule();

    _sti();

    spinlock_acquire(&wq->lock);
    if (!self->wait_node.woken) {
        wq_unlink(wq, &self->wait_node);
    }
    self->blocked_on = NULL;
    spinlock_release(&wq->lock);
}

int waitqueue_wait_timeout(waitqueue_t *wq, uint64_t timeout_ms) {
    (void)timeout_ms;
    warn("waitqueue: timed wait not yet implemented, falling back to untimed wait\n");
    waitqueue_wait(wq);
    return 0;
}

void waitqueue_wake_one(waitqueue_t *wq) {
    spinlock_acquire(&wq->lock);

    wait_node_t *node = wq->head;
    if (!node) {
        spinlock_release(&wq->lock);
        return;
    }

    wq_unlink(wq, node);
    node->woken = true;
    thread_t *thread = node->thread;

    spinlock_release(&wq->lock);

    sched_wake(thread);
}

void waitqueue_wake_all(waitqueue_t *wq) {
    spinlock_acquire(&wq->lock);

    wait_node_t *node = wq->head;
    wq->head = NULL;
    wq->tail = NULL;

    spinlock_release(&wq->lock);

    while (node) {
        wait_node_t *next = node->next;
        node->woken = true;
        node->next = NULL;
        node->prev = NULL;
        sched_wake(node->thread);
        node = next;
    }
}

bool waitqueue_has_waiters(waitqueue_t *wq) {
    spinlock_acquire(&wq->lock);
    bool has = wq->head != NULL;
    spinlock_release(&wq->lock);
    return has;
}
