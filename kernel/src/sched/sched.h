#ifndef _SCHED_H
#define _SCHED_H 1

#include <sched/task.h>
#include <util/spinlock.h>
#include <stdint.h>
#include <stdbool.h>

#define MAX_CPUS 256

typedef struct mlfq_queue {
    thread_t *head;
    thread_t *tail;
    uint32_t count;
} mlfq_queue_t;

typedef struct runqueue {
    spinlock_t lock;
    mlfq_queue_t levels[8];
    uint32_t nr_running;
    thread_t *idle;
    thread_t *current;
    uint64_t last_boost_ns;
    bool needs_resched;
} runqueue_t;

void sched_init(void);
void sched_init_ap(void);
void sched_start(void);

void sched_enqueue(thread_t *thread);
void sched_dequeue(thread_t *thread);

void sched_tick(context_t *ctx);
void sched_check_resched(void);
void schedule(void);
void sched_yield(void);

void sched_mark_blocked(thread_t *thread);
void sched_wake(thread_t *thread);

void sched_sleep_ms(uint64_t ms);
void sched_idle_enter(void);

void sched_set_priority(thread_t *thread, int level);
runqueue_t *sched_rq_for_cpu(int cpu);
void sched_fpu_init(void);

void sched_exit_current(thread_t *self, int exit_code);

uint64_t sched_now_ns(void);


#endif // _SCHED_H
