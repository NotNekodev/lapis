#ifndef _TASK_H
#define _TASK_H

#include "arch/io.h"
#include "mm/vmm.h"
#include "sched/auxv.h"
#include "sched/waitqueue.h"
#include <util/spinlock.h>
#include <stdbool.h>
#include <stdint.h>
#include <fs/vfs/kfile.h>

#define TASK_NAME_MAX 32
#define MAX_AUXV 32
#define FD_TABLE_INITIAL 16
#define FD_TABLE_MAX 4096

typedef enum thread_state {
    THREAD_NEW,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_SLEEPING,
    THREAD_ZOMBIE,
    THREAD_DEAD,
} thread_state_t;

typedef enum proc_state {
    PROC_RUNNING,
    PROC_ZOMBIE,
    PROC_DEAD,
} proc_state_t;

#define PROC_FLAG_USER   (1 << 0)
#define PROC_FLAG_KERNEL (1 << 1)

#define THREAD_CREATE_USER    (1 << 0)
#define THREAD_CREATE_KERNEL  (1 << 1)

typedef struct fpu_state {
    uint8_t region[512] __attribute__((aligned(16)));
    bool used;
    bool dirty;
} fpu_state_t;

typedef enum fd_type {
    FD_NONE,
    FD_FILE,
    FD_DIR,
} fd_type_t;

typedef struct fd_entry {
    fd_type_t type;
    kfile_t *file;

    vnode_t *dir_vnode;
    dirent_t *dir_buf;
    size_t dir_count;
    size_t dir_index;
    size_t dir_off;

    int flags;
    uint32_t refcount;
    spinlock_t lock;
} fd_entry_t;

typedef struct fd_table {
    spinlock_t lock;
    fd_entry_t **entries;

    size_t capacity;
    uint32_t refcount;
} fd_table_t;

struct proc;

typedef struct thread {
    tid_t tid;
    struct proc *proc;

    thread_state_t state;
    int priority_level;
    uint64_t time_in_level_ns;
    uint64_t last_run_tsc;
    uint64_t total_runtime_ns;

    int last_cpu;
    int pinned_cpu;

    void *kstack_base;
    size_t kstack_size;
    uint64_t kernel_rsp;

    context_t *user_ctx;

    fpu_state_t fpu;

    waitqueue_t *blocked_on;
    wait_node_t wait_node;

    int exit_code;

    struct thread *rq_next;
    struct thread *rq_prev;

    struct thread *proc_next;

    spinlock_t lock;
} thread_t;

typedef struct proc {
    pid_t pid;
    char name[TASK_NAME_MAX];

    proc_state_t state;
    uint32_t flags;

    vmm_t *vmm;

    struct proc *parent;
    struct proc *children;
    struct proc *sibling_next;

    thread_t *threads;
    uint32_t thread_count;
    uint32_t live_thread_count;

    fd_table_t *fdtable;

    char **argv;
    int argc;
    char **envp;
    int envc;

    auxv_entry_t auxv[MAX_AUXV];
    int auxv_count;

    int exit_code;
    waitqueue_t wait_children;

    uid_t uid;
    gid_t gid;

    uint32_t refcount;
    spinlock_t lock;

    struct proc *global_next;
} proc_t;

void task_subsystem_init(void);

proc_t *proc_create_kernel(const char *name);
proc_t *proc_lookup(pid_t pid);
void proc_ref(proc_t *proc);
void proc_unref(proc_t *proc);

thread_t *thread_create(proc_t *proc, void (*entry)(void *), void *arg, uint32_t flags);
thread_t *thread_create_user(proc_t *proc, uint64_t entry_rip, uint64_t user_stack_top);
void thread_destroy(thread_t *thread);
void thread_free_resources(thread_t *thread);

pid_t proc_fork(proc_t *parent, thread_t *parent_thread, context_t *parent_ctx);

int proc_set_argv(proc_t *proc, int argc, char **argv, int envc, char **envp);
int proc_set_auxv(proc_t *proc, int key, uintptr_t value);
uintptr_t proc_get_auxv(proc_t *proc, int key);
void proc_reset_for_exec(proc_t *proc);

void proc_exit(proc_t *proc, int exit_code);
void thread_exit(thread_t *thread, int exit_code);
pid_t proc_wait(proc_t *parent, pid_t child_pid, int *status, int options);

fd_table_t *fdtable_create(void);
fd_table_t *fdtable_clone(fd_table_t *src);
void fdtable_ref(fd_table_t *table);
void fdtable_unref(fd_table_t *table);

int fdtable_install(fd_table_t *table, fd_entry_t *entry);
int fdtable_install_at(fd_table_t *table, int fd, fd_entry_t *entry);
fd_entry_t *fdtable_get(fd_table_t *table, int fd);
int fdtable_close(fd_table_t *table, int fd);
int fdtable_dup(fd_table_t *table, int oldfd);
int fdtable_dup2(fd_table_t *table, int oldfd, int newfd);

fd_entry_t *fd_open(const char *path, int flags, mode_t mode);
void fd_entry_ref(fd_entry_t *entry);
void fd_entry_unref(fd_entry_t *entry);
size_t fd_read(fd_entry_t *entry, void *buf, size_t size);
int fd_write(fd_entry_t *entry, void *buf, size_t size);
int fd_readdir(fd_entry_t *entry, dirent_t *out, size_t max);
size_t fd_seek(fd_entry_t *entry, off_t offset, fseek_t whence);

thread_t *thread_current(void);
proc_t *proc_current(void);

proc_t *task_create_init_proc(void);
proc_t *task_get_init_proc(void);
vaddr_t proc_map_user_stack(proc_t *proc);

#endif // _TASH_H
