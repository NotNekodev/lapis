#include <sched/sched.h>
#include <sched/task.h>
#include <sched/waitqueue.h>
#include <arch/cpu.h>
#include <arch/io.h>
#include <arch/gdt/gdt.h>
#include <mm/vmm.h>
#include <util/memory.h>
#include <log/log.h>
#include <kernel.h>
#include <stddef.h>

extern void context_switch(uint64_t *old_rsp_out, uint64_t new_rsp);

static void reap_pending_zombie(void);

static runqueue_t runqueues[MAX_CPUS];
static uint32_t nr_cpus = 1;

static const uint32_t level_quantum_ms[8] = {
    5, 10, 20, 40, 80, 120, 160, 200
};

#define PRIORITY_BOOST_INTERVAL_NS (200ULL * 1000000ULL)

uint64_t sched_now_ns(void) {
    return tsc_get_nanoseconds();
}

static void mlfq_push(mlfq_queue_t *q, thread_t *t) {
    t->rq_next = NULL;
    t->rq_prev = q->tail;

    if (q->tail) {
        q->tail->rq_next = t;
    } else {
        q->head = t;
    }

    q->tail = t;
    q->count++;
}

static thread_t *mlfq_pop(mlfq_queue_t *q) {
    thread_t *t = q->head;
    if (!t) {
        return NULL;
    }

    q->head = t->rq_next;
    if (q->head) {
        q->head->rq_prev = NULL;
    } else {
        q->tail = NULL;
    }

    t->rq_next = NULL;
    t->rq_prev = NULL;
    q->count--;
    return t;
}

static void mlfq_remove(mlfq_queue_t *q, thread_t *t) {
    if (t->rq_prev) {
        t->rq_prev->rq_next = t->rq_next;
    } else if (q->head == t) {
        q->head = t->rq_next;
    }

    if (t->rq_next) {
        t->rq_next->rq_prev = t->rq_prev;
    } else if (q->tail == t) {
        q->tail = t->rq_prev;
    }

    t->rq_next = NULL;
    t->rq_prev = NULL;
    q->count--;
}

runqueue_t *sched_rq_for_cpu(int cpu) {
    if (cpu < 0 || (uint32_t)cpu >= nr_cpus) {
        return NULL;
    }
    return &runqueues[cpu];
}

static runqueue_t *current_rq(void) {
    return get_current_cpu()->rq;
}

static void idle_thread_fn(void *arg) {
    (void)arg;
    for (;;) {
        _sti();
        _hlt();
    }
}

static void runqueue_init_one(runqueue_t *rq, int cpu_index) {
    rq->lock = (spinlock_t)SPINLOCK_INIT("runqueue_lock");
    for (int i = 0; i < 8; i++) {
        rq->levels[i].head = NULL;
        rq->levels[i].tail = NULL;
        rq->levels[i].count = 0;
    }
    rq->nr_running = 0;
    rq->current = NULL;
    rq->last_boost_ns = 0;
    rq->needs_resched = false;

    proc_t *idle_proc = proc_create_kernel("idle");
    thread_t *idle = thread_create(idle_proc, idle_thread_fn, NULL, THREAD_CREATE_KERNEL);
    idle->state = THREAD_RUNNING;
    idle->pinned_cpu = cpu_index;
    idle->last_cpu = cpu_index;
    rq->idle = idle;
}

void sched_init(void) {
    nr_cpus = 1;
    task_subsystem_init();
    runqueue_init_one(&runqueues[0], 0);

    cpu_t *cpu = get_current_cpu();
    cpu->rq = &runqueues[0];
    cpu->idle_thread = runqueues[0].idle;
    cpu->current_thread = runqueues[0].idle;
    runqueues[0].current = runqueues[0].idle;

    sched_fpu_init();
}

void sched_init_ap(void) {
    long id = get_current_cpuid();
    if ((uint32_t)id >= nr_cpus) {
        nr_cpus = id + 1;
    }

    runqueue_init_one(&runqueues[id], (int)id);

    cpu_t *cpu = get_current_cpu();
    cpu->rq = &runqueues[id];
    cpu->idle_thread = runqueues[id].idle;
    cpu->current_thread = runqueues[id].idle;
    runqueues[id].current = runqueues[id].idle;

    sched_fpu_init();
}

void sched_set_priority(thread_t *thread, int level) {
    if (level < 0) {
        level = 0;
    }
    if (level >= 8) {
        level = 8 - 1;
    }
    thread->priority_level = level;
    thread->time_in_level_ns = 0;
}

void sched_enqueue(thread_t *thread) {
    int cpu = thread->pinned_cpu >= 0 ? thread->pinned_cpu : (thread->last_cpu >= 0 ? thread->last_cpu : (int)get_current_cpuid());
    runqueue_t *rq = sched_rq_for_cpu(cpu);
    if (!rq) {
        rq = current_rq();
        cpu = (int)get_current_cpuid();
    }

    spinlock_acquire(&rq->lock);
    thread->state = THREAD_READY;
    thread->last_cpu = cpu;
    mlfq_push(&rq->levels[thread->priority_level], thread);
    rq->nr_running++;
    spinlock_release(&rq->lock);
}

void sched_dequeue(thread_t *thread) {
    runqueue_t *rq = sched_rq_for_cpu(thread->last_cpu);
    if (!rq) {
        return;
    }

    spinlock_acquire(&rq->lock);
    mlfq_remove(&rq->levels[thread->priority_level], thread);
    if (rq->nr_running > 0) {
        rq->nr_running--;
    }
    spinlock_release(&rq->lock);
}

static thread_t *steal_from(runqueue_t *victim) {
    if (victim->nr_running == 0) {
        return NULL;
    }

    for (int lvl = 8 - 1; lvl >= 0; lvl--) {
        mlfq_queue_t *q = &victim->levels[lvl];
        if (q->count > 1) {
            thread_t *t = mlfq_pop(q);
            victim->nr_running--;
            return t;
        }
    }

    return NULL;
}

static thread_t *try_steal(runqueue_t *self_rq) {
    long my_id = get_current_cpuid();

    for (uint32_t i = 0; i < nr_cpus; i++) {
        if ((long)i == my_id) {
            continue;
        }

        runqueue_t *victim = &runqueues[i];
        if (victim->nr_running <= 1) {
            continue;
        }

        runqueue_t *first = self_rq;
        runqueue_t *second = victim;
        if ((uintptr_t)first > (uintptr_t)second) {
            runqueue_t *tmp = first;
            first = second;
            second = tmp;
        }

        spinlock_acquire(&first->lock);
        spinlock_acquire(&second->lock);

        thread_t *stolen = steal_from(victim);

        spinlock_release(&second->lock);
        spinlock_release(&first->lock);

        if (stolen) {
            stolen->last_cpu = (int)my_id;
            return stolen;
        }
    }

    return NULL;
}

static thread_t *mlfq_pick_next(runqueue_t *rq) {
    for (int lvl = 0; lvl < 8; lvl++) {
        if (rq->levels[lvl].count > 0) {
            return mlfq_pop(&rq->levels[lvl]);
        }
    }
    return NULL;
}

static void maybe_boost(runqueue_t *rq) {
    uint64_t now_ns = sched_now_ns();

    if (rq->last_boost_ns == 0) {
        rq->last_boost_ns = now_ns;
        return;
    }

    if (now_ns - rq->last_boost_ns < PRIORITY_BOOST_INTERVAL_NS) {
        return;
    }

    rq->last_boost_ns = now_ns;

    for (int lvl = 1; lvl < 8; lvl++) {
        thread_t *t;
        while ((t = mlfq_pop(&rq->levels[lvl])) != NULL) {
            t->priority_level = 0;
            t->time_in_level_ns = 0;
            mlfq_push(&rq->levels[0], t);
        }
    }
}

static inline uint64_t read_cr0(void) {
    uint64_t v;
    __asm__ volatile("mov %%cr0, %0" : "=r"(v));
    return v;
}

static inline void write_cr0(uint64_t v) {
    __asm__ volatile("mov %0, %%cr0" : : "r"(v));
}

#define CR0_TS (1ULL << 3)

static void fpu_save(thread_t *t) {
    if (!t->fpu.dirty) {
        return;
    }
    __asm__ volatile("fxsave (%0)" : : "r"(t->fpu.region) : "memory");
    t->fpu.dirty = false;
}

static void fpu_lazy_disable(void) {
    write_cr0(read_cr0() | CR0_TS);
}

static void nm_handler(isr_t *self, context_t *ctx) {
    (void)self;
    (void)ctx;

    write_cr0(read_cr0() & ~CR0_TS);

    thread_t *cur = get_current_cpu()->current_thread;

    if (!cur->fpu.used) {
        cur->fpu.used = true;
        memset(cur->fpu.region, 0, sizeof(cur->fpu.region));
        __asm__ volatile("fxrstor (%0)" : : "r"(cur->fpu.region) : "memory");
    } else {
        __asm__ volatile("fxrstor (%0)" : : "r"(cur->fpu.region) : "memory");
    }

    cur->fpu.dirty = true;
}

void sched_fpu_init(void) {
    write_cr0((read_cr0() | CR0_TS) & ~(1ULL << 2));
    register_interrupt(7, nm_handler, NULL);
}

static void switch_address_space(thread_t *next) {
    if (next->proc->vmm) {
        vmm_switch(next->proc->vmm);
    }
}

static void switch_tss(thread_t *next) {
    cpu_t *cpu = get_current_cpu();
    cpu->tss.rsp0 = (uint64_t)next->kstack_base + next->kstack_size;
}

static void do_switch(thread_t *prev, thread_t *next) {
    cpu_t *cpu = get_current_cpu();

    fpu_save(prev);
    fpu_lazy_disable();

    cpu->current_thread = next;
    cpu->rq->current = next;
    next->state = THREAD_RUNNING;
    next->last_run_tsc = _rdtsc();

    switch_address_space(next);
    switch_tss(next);

    context_switch(&prev->kernel_rsp, next->kernel_rsp);
}

void schedule(void) {
    reap_pending_zombie();

    cpu_t *cpu = get_current_cpu();
    runqueue_t *rq = cpu->rq;
    thread_t *prev = cpu->current_thread;

    uint64_t flags = _get_rflags();
    _cli();

    spinlock_acquire(&rq->lock);

    if (prev->state == THREAD_RUNNING) {
        sched_set_priority(prev, prev->priority_level);
        mlfq_push(&rq->levels[prev->priority_level], prev);
        prev->state = THREAD_READY;
        rq->nr_running++;
    }

    maybe_boost(rq);

    thread_t *next = mlfq_pick_next(rq);

    rq->needs_resched = false;

    if (!next) {
        spinlock_release(&rq->lock);
        next = try_steal(rq);
        spinlock_acquire(&rq->lock);
    }

    if (!next) {
        next = rq->idle;
    } else {
        rq->nr_running--;
    }

    spinlock_release(&rq->lock);

    if (next == prev) {
        _set_rflags(flags);
        return;
    }

    do_switch(prev, next);

    _set_rflags(flags);
}

static void reap_pending_zombie(void) {
    cpu_t *cpu = get_current_cpu();
    if (cpu->zombie_to_reap) {
        thread_t *z = cpu->zombie_to_reap;
        cpu->zombie_to_reap = NULL;
        thread_free_resources(z);
    }
}

void sched_yield(void) {
    schedule();
}

void sched_tick(context_t *ctx) {
    (void)ctx;

    cpu_t *cpu = get_current_cpu();
    thread_t *cur = cpu->current_thread;

    if (cur == cpu->idle_thread) {
        cpu->need_resched = 1;
        return;
    }

    uint64_t now = _rdtsc();
    uint64_t delta_ns = (now - cur->last_run_tsc) * 1000000000ULL / (kernel_info.tsc_freq ? kernel_info.tsc_freq : 1);
    cur->time_in_level_ns += delta_ns;
    cur->total_runtime_ns += delta_ns;

    uint64_t quantum_ns = (uint64_t)level_quantum_ms[cur->priority_level] * 1000000ULL;

    if (cur->time_in_level_ns >= quantum_ns) {
        cur->time_in_level_ns = 0;
        if (cur->priority_level < 8 - 1) {
            cur->priority_level++;
        }
        cpu->need_resched = 1;
    }
}

void sched_check_resched(void) {
    cpu_t *cpu = get_current_cpu();
    if (!cpu->rq) {
        return;
    }
    if (cpu->need_resched) {
        cpu->need_resched = 0;
        schedule();
    }
}

void sched_sleep_ms(uint64_t ms) {
    uint64_t deadline_ns = sched_now_ns() + ms * 1000000ULL;
    while (sched_now_ns() < deadline_ns) {
        sched_yield();
    }
}

void sched_mark_blocked(thread_t *thread) {
    spinlock_acquire(&thread->lock);
    thread->state = THREAD_BLOCKED;
    spinlock_release(&thread->lock);
}

void sched_wake(thread_t *thread) {
    spinlock_acquire(&thread->lock);
    if (thread->state != THREAD_BLOCKED && thread->state != THREAD_SLEEPING) {
        spinlock_release(&thread->lock);
        return;
    }
    thread->state = THREAD_READY;
    spinlock_release(&thread->lock);

    sched_enqueue(thread);
}

void sched_exit_current(thread_t *self, int exit_code) {
    cpu_t *cpu = get_current_cpu();
    self->exit_code = exit_code;

    _cli();
    spinlock_acquire(&self->lock);
    self->state = THREAD_ZOMBIE;
    spinlock_release(&self->lock);

    cpu->zombie_to_reap = self;

    schedule();

    critical("sched: exited thread resumed after becoming zombie, halting\n");
    for (;;) {
        _hlt();
    }
}

void sched_idle_enter(void) {
    cpu_t *cpu = get_current_cpu();
    cpu->current_thread = cpu->idle_thread;
    cpu->rq->current = cpu->idle_thread;
    _sti();
    for (;;) {
        _hlt();
    }
}

void sched_start(void) {
    cpu_t *cpu = get_current_cpu();
    thread_t *idle = cpu->idle_thread;

    switch_tss(idle);
    _sti();
}
