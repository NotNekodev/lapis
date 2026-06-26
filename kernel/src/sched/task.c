#include <sched/task.h>
#include <sched/sched.h>
#include <mm/kheap.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <mm/pfn_db.h>
#include <mm/page.h>
#include <mm/paging.h>
#include <arch/cpu.h>
#include <arch/io.h>
#include <util/memory.h>
#include <util/errno.h>
#include <log/log.h>
#include <kernel.h>
#include <stddef.h>

static spinlock_t proc_list_lock = SPINLOCK_INIT("proc_list_lock");
static proc_t *proc_list_head;
static pid_t next_pid = 1;

static spinlock_t tid_lock = SPINLOCK_INIT("tid_lock");
static tid_t next_tid = 1;

static proc_t *init_proc;

static void proc_remove_child(proc_t *parent, proc_t *child);
static int copy_argv_envp(proc_t *dst, char **argv, int argc, char **envp, int envc);

extern void context_switch(uint64_t *old_rsp_out, uint64_t new_rsp);
extern void thread_bootstrap_trampoline(void);
extern void fork_child_entry(void);
extern void usermode_enter(uint64_t entry_rip, uint64_t user_stack_top);

static uint64_t build_bootstrap_frame(void *stack_top, uint64_t return_addr, uint64_t rbx_val, uint64_t r12_val) {
    uint64_t *sp = (uint64_t *)stack_top;
    sp -= 1; *sp = return_addr;
    sp -= 1; *sp = 0;
    sp -= 1; *sp = rbx_val;
    sp -= 1; *sp = r12_val;
    sp -= 1; *sp = 0;
    sp -= 1; *sp = 0;
    sp -= 1; *sp = 0;
    sp -= 1; *sp = 0x202;
    return (uint64_t)sp;
}

static pid_t alloc_pid(void) {
    spinlock_acquire(&proc_list_lock);
    pid_t pid = next_pid++;
    spinlock_release(&proc_list_lock);
    return pid;
}

static tid_t alloc_tid(void) {
    spinlock_acquire(&tid_lock);
    tid_t tid = next_tid++;
    spinlock_release(&tid_lock);
    return tid;
}

static void proc_list_add(proc_t *proc) {
    spinlock_acquire(&proc_list_lock);
    proc->global_next = proc_list_head;
    proc_list_head = proc;
    spinlock_release(&proc_list_lock);
}

static void proc_list_remove(proc_t *proc) {
    spinlock_acquire(&proc_list_lock);
    proc_t **pp = &proc_list_head;
    while (*pp) {
        if (*pp == proc) {
            *pp = proc->global_next;
            break;
        }
        pp = &(*pp)->global_next;
    }
    spinlock_release(&proc_list_lock);
}

proc_t *proc_lookup(pid_t pid) {
    spinlock_acquire(&proc_list_lock);
    proc_t *p = proc_list_head;
    while (p) {
        if (p->pid == pid) {
            break;
        }
        p = p->global_next;
    }
    spinlock_release(&proc_list_lock);
    return p;
}

void task_subsystem_init(void) {
    proc_list_head = NULL;
}

void proc_ref(proc_t *proc) {
    if (!proc) {
        return;
    }
    spinlock_acquire(&proc->lock);
    proc->refcount++;
    spinlock_release(&proc->lock);
}

static void proc_free(proc_t *proc) {
    if (proc->fdtable) {
        fdtable_unref(proc->fdtable);
    }

    if (proc->vmm && !(proc->flags & PROC_FLAG_KERNEL)) {
        vmm_destroy(proc->vmm);
    }

    if (proc->argv) {
        for (int i = 0; i < proc->argc; i++) {
            kfree(proc->argv[i]);
        }
        kfree(proc->argv);
    }

    if (proc->envp) {
        for (int i = 0; i < proc->envc; i++) {
            kfree(proc->envp[i]);
        }
        kfree(proc->envp);
    }

    kfree(proc);
}

void proc_unref(proc_t *proc) {
    if (!proc) {
        return;
    }

    spinlock_acquire(&proc->lock);
    proc->refcount--;
    uint32_t remaining = proc->refcount;
    spinlock_release(&proc->lock);

    if (remaining == 0) {
        proc_list_remove(proc);
        proc_free(proc);
    }
}

static proc_t *proc_alloc(const char *name, uint32_t flags) {
    proc_t *proc = kzalloc(sizeof(proc_t));
    if (!proc) {
        return NULL;
    }

    proc->pid = alloc_pid();
    proc->flags = flags;
    proc->state = PROC_RUNNING;
    proc->refcount = 1;
    proc->lock = (spinlock_t)SPINLOCK_INIT("proc_lock");
    waitqueue_init(&proc->wait_children, "proc_wait_children");

    if (name) {
        size_t len = strlen(name);
        if (len >= TASK_NAME_MAX) {
            len = TASK_NAME_MAX - 1;
        }
        memcpy(proc->name, name, len);
        proc->name[len] = '\0';
    }

    proc_list_add(proc);
    return proc;
}

proc_t *proc_create_kernel(const char *name) {
    proc_t *proc = proc_alloc(name, PROC_FLAG_KERNEL);
    if (!proc) {
        return NULL;
    }

    proc->vmm = vmm_kernel();
    proc->fdtable = fdtable_create();

    if (!proc->fdtable) {
        proc_unref(proc);
        return NULL;
    }

    return proc;
}

static void *kstack_alloc(size_t size, page_t **out_first_page) {
    size_t npages = size / PAGE_SIZE;
    page_t *pages = pmm_alloc_pages(npages);
    if (!pages) {
        return NULL;
    }

    uint64_t phys = pfn_db_page_to_phys(pages);
    void *virt = PHYS_TO_VIRT(phys);
    memset(virt, 0, size);

    if (out_first_page) {
        *out_first_page = pages;
    }

    return virt;
}

static void kstack_free(void *base, size_t size) {
    if (!base) {
        return;
    }

    uint64_t phys = VIRT_TO_PHYS(base);
    page_t *page = pfn_db_phys_to_page(phys);
    if (!page) {
        critical("task: invalid kstack base %p on free\n", base);
        return;
    }

    pmm_pages_release(page, size / PAGE_SIZE);
}

static void thread_link_to_proc(proc_t *proc, thread_t *thread) {
    spinlock_acquire(&proc->lock);
    thread->proc_next = proc->threads;
    proc->threads = thread;
    proc->thread_count++;
    proc->live_thread_count++;
    spinlock_release(&proc->lock);
}

thread_t *thread_create(proc_t *proc, void (*entry)(void *), void *arg, uint32_t flags) {
    (void)flags;
    if (!proc) {
        return NULL;
    }

    thread_t *thread = kzalloc(sizeof(thread_t));
    if (!thread) {
        return NULL;
    }

    thread->kstack_base = kstack_alloc(KSTACK_SIZE, NULL);
    if (!thread->kstack_base) {
        kfree(thread);
        return NULL;
    }

    thread->kstack_size = KSTACK_SIZE;
    thread->tid = alloc_tid();
    thread->proc = proc;
    thread->state = THREAD_NEW;
    thread->priority_level = 0;
    thread->last_cpu = -1;
    thread->pinned_cpu = -1;
    thread->lock = (spinlock_t)SPINLOCK_INIT("thread_lock");

    uint64_t stack_top = (uint64_t)thread->kstack_base + thread->kstack_size;
    stack_top &= ~0xFULL;

    thread->kernel_rsp = build_bootstrap_frame((void *)(uintptr_t)stack_top,
        (uint64_t)thread_bootstrap_trampoline, (uint64_t)entry, (uint64_t)arg);

    thread_link_to_proc(proc, thread);

    return thread;
}

#define USER_STACK_SIZE (VMM_STACK_SIZE)

static void thread_user_entry_stub(void *arg) {
    uint64_t *boot_args = (uint64_t *)arg;
    uint64_t entry_rip = boot_args[0];
    uint64_t user_stack_top = boot_args[1];
    kfree(boot_args);

    usermode_enter(entry_rip, user_stack_top);
}

thread_t *thread_create_user(proc_t *proc, uint64_t entry_rip, uint64_t user_stack_top) {
    if (!proc || !(proc->flags & PROC_FLAG_USER)) {
        warn("task: thread_create_user called on a non-user proc\n");
        return NULL;
    }

    uint64_t *boot_args = kzalloc(sizeof(uint64_t) * 2);
    if (!boot_args) {
        return NULL;
    }

    boot_args[0] = entry_rip;
    boot_args[1] = user_stack_top;

    thread_t *thread = thread_create(proc, thread_user_entry_stub, boot_args, THREAD_CREATE_USER);
    if (!thread) {
        kfree(boot_args);
        return NULL;
    }

    return thread;
}

vaddr_t proc_map_user_stack(proc_t *proc) {
    vaddr_t base = vmm_map(proc->vmm, 0, USER_STACK_SIZE,
        VFLAG_PRESENT | VFLAG_WRITABLE | VFLAG_USER | VFLAG_STACK);

    if (!base) {
        return 0;
    }

    vaddr_t guard_addr = base - VMM_GUARD_SIZE;
    vaddr_t guard = vmm_map(proc->vmm, guard_addr, VMM_GUARD_SIZE, VFLAG_FIXED);

    if (!guard) {
        warn("task: failed to map stack guard page at 0x%zx, continuing without it\n", guard_addr);
    }

    return base + USER_STACK_SIZE;
}

static thread_t *thread_fork_from_syscall(proc_t *child_proc, thread_t *parent_thread, context_t *parent_ctx) {
    thread_t *child = kzalloc(sizeof(thread_t));
    if (!child) {
        return NULL;
    }

    child->kstack_base = kstack_alloc(KSTACK_SIZE, NULL);
    if (!child->kstack_base) {
        kfree(child);
        return NULL;
    }

    child->kstack_size = KSTACK_SIZE;
    child->tid = alloc_tid();
    child->proc = child_proc;
    child->state = THREAD_NEW;
    child->priority_level = parent_thread->priority_level;
    child->last_cpu = -1;
    child->pinned_cpu = -1;
    child->lock = (spinlock_t)SPINLOCK_INIT("thread_lock");
    memcpy(&child->fpu, &parent_thread->fpu, sizeof(child->fpu));

    uint64_t stack_top = (uint64_t)child->kstack_base + child->kstack_size;

    context_t *child_ctx = (context_t *)(uintptr_t)(stack_top - sizeof(context_t));
    memcpy(child_ctx, parent_ctx, sizeof(context_t));
    child_ctx->rax = 0;

    child->kernel_rsp = build_bootstrap_frame(child_ctx, (uint64_t)fork_child_entry, (uint64_t)child_ctx, 0);

    thread_link_to_proc(child_proc, child);

    return child;
}

static void proc_add_child(proc_t *parent, proc_t *child) {
    spinlock_acquire(&parent->lock);
    child->sibling_next = parent->children;
    parent->children = child;
    spinlock_release(&parent->lock);
}

pid_t proc_fork(proc_t *parent, thread_t *parent_thread, context_t *parent_ctx) {
    if (!parent || !parent_thread || !parent_ctx) {
        return -EINVAL;
    }

    proc_t *child = proc_alloc(parent->name, parent->flags);
    if (!child) {
        return -ENOMEM;
    }

    child->vmm = vmm_fork(parent->vmm);
    if (!child->vmm) {
        proc_unref(child);
        return -ENOMEM;
    }

    child->fdtable = fdtable_clone(parent->fdtable);
    if (!child->fdtable) {
        proc_unref(child);
        return -ENOMEM;
    }

    if (parent->argv) {
        copy_argv_envp(child, parent->argv, parent->argc, parent->envp, parent->envc);
    }

    memcpy(child->auxv, parent->auxv, sizeof(parent->auxv));
    child->auxv_count = parent->auxv_count;

    child->uid = parent->uid;
    child->gid = parent->gid;

    child->parent = parent;
    proc_add_child(parent, child);

    thread_t *child_thread = thread_fork_from_syscall(child, parent_thread, parent_ctx);
    if (!child_thread) {
        proc_remove_child(parent, child);
        proc_unref(child);
        return -ENOMEM;
    }

    pid_t child_pid = child->pid;

    child_thread->state = THREAD_READY;
    sched_enqueue(child_thread);

    return child_pid;
}

thread_t *thread_current(void) {
    return get_current_cpu()->current_thread;
}

proc_t *proc_current(void) {
    thread_t *t = thread_current();
    return t ? t->proc : NULL;
}

static char **dup_string_array(char **src, int count) {
    if (!src || count <= 0) {
        return NULL;
    }

    char **dst = kzalloc(sizeof(char *) * (size_t)count);
    if (!dst) {
        return NULL;
    }

    for (int i = 0; i < count; i++) {
        size_t len = strlen(src[i]);
        dst[i] = kmalloc(len + 1);
        if (!dst[i]) {
            for (int j = 0; j < i; j++) {
                kfree(dst[j]);
            }
            kfree(dst);
            return NULL;
        }
        memcpy(dst[i], src[i], len + 1);
    }

    return dst;
}

static void free_string_array(char **arr, int count) {
    if (!arr) {
        return;
    }
    for (int i = 0; i < count; i++) {
        kfree(arr[i]);
    }
    kfree(arr);
}

static int copy_argv_envp(proc_t *dst, char **argv, int argc, char **envp, int envc) {
    char **new_argv = dup_string_array(argv, argc);
    if (argv && argc > 0 && !new_argv) {
        return -ENOMEM;
    }

    char **new_envp = dup_string_array(envp, envc);
    if (envp && envc > 0 && !new_envp) {
        free_string_array(new_argv, argc);
        return -ENOMEM;
    }

    dst->argv = new_argv;
    dst->argc = argc;
    dst->envp = new_envp;
    dst->envc = envc;

    return EOK;
}

int proc_set_argv(proc_t *proc, int argc, char **argv, int envc, char **envp) {
    if (!proc) {
        return -EINVAL;
    }

    free_string_array(proc->argv, proc->argc);
    free_string_array(proc->envp, proc->envc);
    proc->argv = NULL;
    proc->envp = NULL;
    proc->argc = 0;
    proc->envc = 0;

    return copy_argv_envp(proc, argv, argc, envp, envc);
}

int proc_set_auxv(proc_t *proc, int key, uintptr_t value) {
    if (!proc) {
        return -EINVAL;
    }

    spinlock_acquire(&proc->lock);

    for (int i = 0; i < proc->auxv_count; i++) {
        if (proc->auxv[i].key == key) {
            proc->auxv[i].value = value;
            spinlock_release(&proc->lock);
            return EOK;
        }
    }

    if (proc->auxv_count >= MAX_AUXV) {
        spinlock_release(&proc->lock);
        return -ENOMEM;
    }

    proc->auxv[proc->auxv_count].key = key;
    proc->auxv[proc->auxv_count].value = value;
    proc->auxv_count++;

    spinlock_release(&proc->lock);
    return EOK;
}

uintptr_t proc_get_auxv(proc_t *proc, int key) {
    if (!proc) {
        return 0;
    }

    spinlock_acquire(&proc->lock);

    uintptr_t result = 0;
    for (int i = 0; i < proc->auxv_count; i++) {
        if (proc->auxv[i].key == key) {
            result = proc->auxv[i].value;
            break;
        }
    }

    spinlock_release(&proc->lock);
    return result;
}

void proc_reset_for_exec(proc_t *proc) {
    if (!proc) {
        return;
    }

    free_string_array(proc->argv, proc->argc);
    free_string_array(proc->envp, proc->envc);
    proc->argv = NULL;
    proc->envp = NULL;
    proc->argc = 0;
    proc->envc = 0;
    proc->auxv_count = 0;

    if (proc->thread_count > 1) {
        critical("task: proc_reset_for_exec on a multi-threaded process is not implemented, "
                 "other threads must be torn down via an exit-and-wait protocol before calling this\n");
        return;
    }

    if (proc->vmm && !(proc->flags & PROC_FLAG_KERNEL)) {
        vmm_destroy(proc->vmm);
    }
    proc->vmm = vmm_create();
}

void thread_free_resources(thread_t *thread) {
    kstack_free(thread->kstack_base, thread->kstack_size);
    kfree(thread);
}

static void proc_unlink_thread(proc_t *proc, thread_t *thread) {
    spinlock_acquire(&proc->lock);
    thread_t **pp = &proc->threads;
    while (*pp) {
        if (*pp == thread) {
            *pp = thread->proc_next;
            break;
        }
        pp = &(*pp)->proc_next;
    }
    spinlock_release(&proc->lock);
}

void thread_exit(thread_t *thread, int exit_code) {
    if (!thread) {
        return;
    }

    proc_t *proc = thread->proc;
    bool is_self = (thread == thread_current());

    spinlock_acquire(&thread->lock);
    thread->exit_code = exit_code;
    thread->state = THREAD_ZOMBIE;
    spinlock_release(&thread->lock);

    spinlock_acquire(&proc->lock);
    if (proc->live_thread_count > 0) {
        proc->live_thread_count--;
    }
    bool last_thread = (proc->live_thread_count == 0);
    spinlock_release(&proc->lock);

    if (last_thread) {
        proc_exit(proc, exit_code);
    }

    proc_unlink_thread(proc, thread);

    if (is_self) {
        sched_exit_current(thread, exit_code);
    } else {
        thread_free_resources(thread);
    }
}

void thread_destroy(thread_t *thread) {
    if (!thread) {
        return;
    }

    spinlock_acquire(&thread->lock);
    thread_state_t state = thread->state;
    spinlock_release(&thread->lock);

    if (state != THREAD_ZOMBIE && state != THREAD_DEAD) {
        critical("task: thread_destroy called on a thread that is not a zombie (tid=%d state=%d)\n", thread->tid, state);
        return;
    }

    if (thread == thread_current()) {
        critical("task: thread_destroy called on the currently running thread\n");
        return;
    }

    thread_free_resources(thread);
}

void thread_exit_current(void) {
    thread_exit(thread_current(), 0);
}

static void proc_remove_child(proc_t *parent, proc_t *child) {
    spinlock_acquire(&parent->lock);
    proc_t **pp = &parent->children;
    while (*pp) {
        if (*pp == child) {
            *pp = child->sibling_next;
            break;
        }
        pp = &(*pp)->sibling_next;
    }
    spinlock_release(&parent->lock);
}

void proc_exit(proc_t *proc, int exit_code) {
    spinlock_acquire(&proc->lock);
    if (proc->state == PROC_ZOMBIE || proc->state == PROC_DEAD) {
        spinlock_release(&proc->lock);
        return;
    }
    proc->state = PROC_ZOMBIE;
    proc->exit_code = exit_code;
    spinlock_release(&proc->lock);

    proc_t *child = proc->children;
    while (child) {
        proc_t *next = child->sibling_next;

        spinlock_acquire(&child->lock);
        child->parent = init_proc;
        spinlock_release(&child->lock);

        if (init_proc) {
            proc_add_child(init_proc, child);
        }

        child = next;
    }
    proc->children = NULL;

    if (proc->parent) {
        waitqueue_wake_all(&proc->parent->wait_children);
    }

    if (init_proc) {
        waitqueue_wake_all(&init_proc->wait_children);
    }
}

pid_t proc_wait(proc_t *parent, pid_t child_pid, int *status, int options) {
    (void)options;

    for (;;) {
        spinlock_acquire(&parent->lock);

        proc_t *found = NULL;
        proc_t *prev = NULL;
        proc_t *iter = parent->children;

        while (iter) {
            bool matches = (child_pid == -1) || (iter->pid == child_pid);
            if (matches && iter->state == PROC_ZOMBIE) {
                found = iter;
                break;
            }
            prev = iter;
            iter = iter->sibling_next;
        }

        bool has_matching_child = false;
        if (!found) {
            for (iter = parent->children; iter; iter = iter->sibling_next) {
                if (child_pid == -1 || iter->pid == child_pid) {
                    has_matching_child = true;
                    break;
                }
            }
        }

        if (!found && !has_matching_child) {
            spinlock_release(&parent->lock);
            return -ECHILD;
        }

        if (found) {
            if (prev) {
                prev->sibling_next = found->sibling_next;
            } else {
                parent->children = found->sibling_next;
            }
            spinlock_release(&parent->lock);

            pid_t pid = found->pid;
            if (status) {
                *status = found->exit_code;
            }

            found->state = PROC_DEAD;
            proc_unref(found);

            return pid;
        }

        spinlock_release(&parent->lock);

        waitqueue_wait(&parent->wait_children);
    }
}

static void init_proc_thread_fn(void *arg) {
    (void)arg;

    for (;;) {
        int status;
        pid_t reaped = proc_wait(init_proc, -1, &status, 0);
        if (reaped == -ECHILD) {
            sched_yield();
            continue;
        }
        debug("init: reaped orphan pid=%d status=%d\n", reaped, status);
    }
}

proc_t *task_create_init_proc(void) {
    init_proc = proc_create_kernel("init");
    if (!init_proc) {
        critical("task: failed to create init process\n");
        return NULL;
    }

    thread_t *t = thread_create(init_proc, init_proc_thread_fn, NULL, THREAD_CREATE_KERNEL);
    if (!t) {
        critical("task: failed to create init thread\n");
        return NULL;
    }

    t->state = THREAD_READY;
    sched_enqueue(t);

    return init_proc;
}

proc_t *task_get_init_proc(void) {
    return init_proc;
}
