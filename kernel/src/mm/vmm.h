#ifndef _VMM_H
#define _VMM_H 1

#include <util/rbtree.h>

#include <stdint.h>
#include <stddef.h>

#define VFLAG_PRESENT       (1 << 0)
#define VFLAG_WRITABLE      (1 << 1)
#define VFLAG_USER          (1 << 2)
#define VFLAG_EXECUTABLE    (1 << 3)
#define VFLAG_LAZY          (1 << 4)
#define VFLAG_ALLOCATED     (1 << 5)
#define VFLAG_SHARED        (1 << 6)
#define VFLAG_FIXED         (1 << 7)
#define VFLAG_STACK         (1 << 8)
#define VFLAG_MMIO          (1 << 9)

// vmm wide flags, so not per vma flags
#define VMM_FLAG_OWNS_PML4  (1 << 0)

#define VMM_USER_START      0x0000000000001000UL
#define VMM_USER_END        0x00007FFFFFFFF000UL
#define VMM_MMAP_BASE       0x0000700000000000UL
#define VMM_STACK_TOP       0x00007FFFFFFFE000UL
#define VMM_STACK_SIZE      (8UL * 1024 * 1024)
#define VMM_GUARD_SIZE      0x1000UL

typedef uintptr_t vaddr_t; // using this because it can be inplicitly casted to and from void* :3

typedef struct vma {
    vaddr_t base;
    size_t size; // in bytes, but always a multiple of PAGE_SIZE
    uint32_t flags;

    void *vnode_backing; // soon when vfs :tm:
    size_t vnode_offset;

    rb_node_t rbn;
} vma_t;

typedef struct vmm {
    rb_tree_t vmas;
    size_t vmas_count;

    uint64_t* pml4;

    uint32_t flags;

    // we LOVE bookkeeping 
    vaddr_t mmap_base;
    vaddr_t stack_top;
    vaddr_t brk;
    vaddr_t brk_base;
} vmm_t;

vmm_t *vmm_create(void);
vmm_t *vmm_create_from_pml4(uint64_t *pml4);
void vmm_destroy(vmm_t *vmm);

vmm_t* vmm_fork(vmm_t *parent);

vmm_t *vmm_current(void);
vmm_t *vmm_kernel(void);

void vmm_switch(vmm_t *vmm);
void vmm_set_kernel(vmm_t *vmm);

vaddr_t vmm_map(vmm_t *vmm, vaddr_t base, size_t size, uint32_t flags);
int vmm_unmap(vmm_t *vmm, vaddr_t base, size_t size);
int vmm_protect(vmm_t *vmm, vaddr_t base, size_t size, uint32_t new_flags);
vaddr_t vmm_brk(vmm_t *vmm, vaddr_t new_brk);

vma_t *vmm_find(vmm_t *vmm, vaddr_t addr);
vma_t *vmm_find_next(vmm_t *vmm, vaddr_t addr);
int vmm_is_accessible(vmm_t *vmm, vaddr_t addr, size_t size, uint32_t access_flags);

int vmm_pf_handler(vmm_t *vmm, vaddr_t fault_addr, uint64_t error_code);
void vmm_dump(vmm_t *vmm);

#endif // _VMM_H