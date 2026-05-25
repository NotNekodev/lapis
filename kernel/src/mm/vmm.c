#include "log/log.h"
#include "mm/page.h"
#include "mm/pfn_db.h"
#include "mm/pmm.h"
#include "util/errno.h"
#include "util/rbtree.h"
#include <mm/vmm.h>
#include <mm/paging.h>
#include <mm/kheap.h>

#include <util/memory.h>
#include <util/spinlock.h>

static vmm_t *current_vmm;
static spinlock_t vmm_lock = SPINLOCK_INIT("vmm_lock");

static uint64_t _vflags_to_pflags(uint32_t vflags){
    uint64_t pflags = 0;

    if (vflags & VFLAG_PRESENT) {
        pflags |= PFLAG_PRESENT;
    }
    if (vflags & VFLAG_WRITABLE) {
        pflags |= PFLAG_WRITE;
    }
    if (vflags & VFLAG_USER) {
        pflags |= PFLAG_USER;
    }
    if (!(vflags & VFLAG_EXECUTABLE)) {
        pflags |= PFLAG_NX;
    }
    if (vflags & VFLAG_MMIO) {
        pflags |= PFLAG_PCD | PFLAG_PWT;
    }

    return pflags;
}

static vma_t *_vma_alloc(void) {
    return kzalloc(sizeof(vma_t));
}

static void _vma_free(vma_t *vma) {
    kfree(vma);
}

static int _vma_cmp(const void *a, const void *b) {
    const vma_t *va = containerof((const rb_node_t*)a, vma_t, rbn);
    const vma_t *vb = containerof((const rb_node_t*)b, vma_t, rbn);

    if (va->base < vb->base) {
        return -1;
    } else if (va->base > vb->base) {
        return 1;
    } else {
        return 0;
    }
}

static vma_t *_find_vma(vmm_t *vmm, vaddr_t addr) {
    vma_t key = { .base = addr };
    rb_node_t *node = rb_lower_bound(&vmm->vmas, &key.rbn);

    if (node) {
        vma_t *v = containerof(node, vma_t, rbn);

        if (v->base <= addr && addr < v->base + v->size) {
            return v;
        }

        rb_node_t *prev = rb_prev(node);
        if (prev) {
            v = containerof(prev, vma_t, rbn);

            if (v->base <= addr && addr < v->base + v->size) {
                return v;
            }
        }
    } else {
        rb_node_t *last = rb_last(&vmm->vmas);
        if (last) {
            vma_t *v = containerof(last, vma_t, rbn);

            if (v->base <= addr && addr < v->base + v->size) {
                return v;
            }
        }
    }

    return NULL;
}

static vma_t *_find_vma_next(vmm_t *vmm, vaddr_t addr) {
    vma_t key = { .base = addr };
    rb_node_t *node = rb_lower_bound(&vmm->vmas, &key.rbn);

    if (node) {
        return containerof(node, vma_t, rbn);
    } else {
        return NULL;
    }
}

static vaddr_t _find_free_gap(vmm_t *vmm, size_t size) {
    vaddr_t hint = vmm->mmap_base;

    for (;;) {
        if (hint < VMM_USER_START + size) {
            return 0;
        }

        vaddr_t candidate = (hint - size) & ~(vaddr_t)(PAGE_SIZE - 1);

        if (candidate < VMM_USER_START) {
            return 0;
        }

        vma_t *overlap = _find_vma(vmm, candidate);
        if (!overlap) {
            overlap = _find_vma(vmm, candidate + size - 1);
        }

        if (!overlap) {
            vma_t *next = _find_vma_next(vmm, candidate);

            if (!next || next->base >= candidate + size) {
                vmm->mmap_base = candidate;
                return candidate;
            }

            hint = next->base;
        } else {
            hint = overlap->base;
        }
    }
}

static int _vma_allocate_pages(vmm_t *vmm, vma_t *vma) {
    uint64_t pflags = _vflags_to_pflags(vma->flags);

    if (vma->size % PAGE_SIZE != 0) {
        warn("vmm: vma size is not a multiple of PAGE_SIZE\n");
        return -1;
    }

    size_t npages = vma->size / PAGE_SIZE; // this should and will always work if we did everything correctly

    for (size_t i = 0; i < npages; i++) {
        vaddr_t vaddr = vma->base + i * PAGE_SIZE;
        page_t *page = pmm_alloc_page();

        if (!page) {
            for (size_t j = 0; j < i; j++) {
                unmap_page(vmm->pml4, vma->base + j * PAGE_SIZE);
            }

            return ENOMEM;
        }

        uint64_t phys = pfn_db_page_to_phys(page);
        memset(PHYS_TO_VIRT(phys), 0, PAGE_SIZE);

        map_page(vmm->pml4, vaddr, page, pflags);
    }

    vma->flags |= VFLAG_ALLOCATED;
    return EOK;
}

static void _vma_free_pages(vmm_t *vmm, vma_t *vma) {
    if (!(vma->flags & VFLAG_ALLOCATED)) {
        return;
    }

    if (vma->size % PAGE_SIZE != 0) {
        warn("vmm: vma size is not a multiple of PAGE_SIZE\n");
        return;
    }

    size_t npages = vma->size / PAGE_SIZE;

    for (size_t i = 0; i < npages; i++) {
        unmap_page(vmm->pml4, vma->base + i * PAGE_SIZE);
    }

    vma->flags &= ~VFLAG_ALLOCATED;
}

static int _range_overlaps(vmm_t *vmm, vaddr_t base, size_t size) {
    vma_t *vma = _find_vma(vmm, base);
    if (!vma) {
        return 0;
    }

    return (vma->base < base + size);
}

vmm_t* vmm_create(void) {
    vmm_t *vmm = kzalloc(sizeof(vmm_t));
    if (!vmm) {
        return NULL;
    }

    vmm->vmas = (rb_tree_t)RB_TREE_INIT(_vma_cmp);
    vmm->pml4 = pte_alloc();
    vmm->mmap_base = VMM_MMAP_BASE;
    vmm->stack_top = VMM_STACK_TOP;
    vmm->flags = VMM_FLAG_OWNS_PML4;

    if (!vmm->pml4) {
        kfree(vmm);
        return NULL;
    }

    return vmm;
}

void vmm_destroy(vmm_t *vmm) {
    if (!vmm) {
        return;
    }

    if (vmm == current_vmm) {
        warn("vmm: destroying current vmm, switching to kernel vmm\n");
        vmm_switch(vmm_kernel());
    }

    rb_node_t *node;
    rb_node_t *tmp;
    rb_for_each_safe(node, tmp, &vmm->vmas) {
        vma_t *vma = containerof(node, vma_t, rbn);
        _vma_free_pages(vmm, vma);
        rb_remove(&vmm->vmas, &vma->rbn);
        _vma_free(vma);
    }

    if (vmm->flags & VMM_FLAG_OWNS_PML4) {
        pte_free((pte_t *)vmm->pml4);
    }
    
    kfree(vmm);
}

void vmm_switch(vmm_t *vmm) {
    if (!vmm || !vmm->pml4) {
        critical("vmm: cannot switch to invalid vmm\n");
        return;
    }

    if (vmm == current_vmm) {
        return;
    }

    current_vmm = vmm;
    __asm__ volatile("mov %0, %%cr3" ::"r"(vmm->pml4) : "memory");
}

vmm_t *vmm_current(void) {
    return current_vmm;
}

vmm_t *vmm_kernel(void) {
    return kernel_info.kernel_vmm;
}

void vmm_set_kernel(vmm_t *vmm) {
    kernel_info.kernel_vmm = vmm;
    current_vmm = vmm;
}

vaddr_t vmm_map(vmm_t *vmm, vaddr_t base, size_t size, uint32_t flags) {
    if (!vmm || size == 0) {
        warn("vmm: cannot map with invalid vmm or size 0\n");
        return 0;
    }

    size = ALIGN_UP(size, PAGE_SIZE);

    spinlock_acquire(&vmm_lock);

    if (base == 0 && !(flags & VFLAG_FIXED)) {
        base = _find_free_gap(vmm, size);
        if (base == 0) {
            warn("vmm: failed to find free gap for mapping with size=0x%zx\n");
            spinlock_release(&vmm_lock);
            return 0;
        }
    } else {
        base = ALIGN_DOWN(base, PAGE_SIZE);

        if (flags & VFLAG_FIXED && _range_overlaps(vmm, base, size)) {
            warn("vmm: VFLAG_FIXED mapping overlaps with existing mapping at base=0x%zx size=0x%zx\n", base, size);
            spinlock_release(&vmm_lock);
            return 0;
        }
    }

    vma_t *vma = _vma_alloc();
    if (!vma) {
        spinlock_release(&vmm_lock);
        return 0;
    }

    vma->base = base;
    vma->size = size;
    vma->flags = flags;
    vma->vnode_backing = NULL;
    vma->vnode_offset = 0;

    if (!(flags & VFLAG_LAZY)) {
        if (_vma_allocate_pages(vmm, vma) != EOK) {
            _vma_free(vma);
            spinlock_release(&vmm_lock);
            return 0;
        }
    }

    if (rb_insert(&vmm->vmas, &vma->rbn) != 0) {
        _vma_free_pages(vmm, vma);
        _vma_free(vma);
        spinlock_release(&vmm_lock);
        return 0;
    }

    vmm->vmas_count++;
    spinlock_release(&vmm_lock);
    return base;
}

static vma_t* _vma_split(vmm_t *vmm, vma_t *vma, vaddr_t split_addr) {
    if (split_addr <= vma->base || split_addr >= vma->base + vma->size) {
        return NULL;
    }

    vma_t *new_vma = _vma_alloc();
    if (!new_vma) {
        return NULL;
    }

    new_vma->base = split_addr;
    new_vma->size = vma->base + vma->size - split_addr;
    new_vma->flags = vma->flags;
    new_vma->vnode_backing = NULL;
    new_vma->vnode_offset = 0;

    vma->size = split_addr - vma->base;

    if (rb_insert(&vmm->vmas, &new_vma->rbn) != 0) {
        _vma_free(new_vma);
        return NULL;
    }

    vmm->vmas_count++;
    return new_vma;
}

int vmm_unmap(vmm_t *vmm, vaddr_t base, size_t size) {
    if (!vmm || size == 0) {
        warn("vmm: cannot unmap with invalid vmm or size 0\n");
        return -EINVAL;
    }

    base = ALIGN_DOWN(base, PAGE_SIZE);
    size = ALIGN_UP(size, PAGE_SIZE);
    vaddr_t end = base + size;

    spinlock_acquire(&vmm_lock);

    vma_t *first = _find_vma(vmm, base);
    if (first && first->base < base) {
        if (!_vma_split(vmm, first, base)) {
            warn("vmm: failed to split vma during unmap at base=0x%zx size=0x%zx\n", base, size);
            spinlock_release(&vmm_lock);
            return -ENOMEM;
        }
    }

    vma_t *last = _find_vma(vmm, end - 1);
    if (last && last->base + last->size < end) {
        if (!_vma_split(vmm, last, end)) {
            warn("vmm: failed to split vma during unmap at base=0x%zx size=0x%zx\n", base, size);
            spinlock_release(&vmm_lock);
            return -ENOMEM;
        }
    }

    rb_node_t *node = rb_first(&vmm->vmas);
    while (node) {
        vma_t *vma = containerof(node, vma_t, rbn);
        rb_node_t *next = rb_next(node);
    
        if (vma->base >= base && vma->base + vma->size <= end) {
            _vma_free_pages(vmm, vma);
            rb_remove(&vmm->vmas, &vma->rbn);
            _vma_free(vma);
            vmm->vmas_count--;
        } else if (vma->base >= end) {
            break;
        }

        node = next;
    }

    spinlock_release(&vmm_lock);
    return 0;
}

int vmm_protect(vmm_t *vmm, vaddr_t base, size_t size, uint32_t new_flags) {
    if (!vmm || size == 0) {
        warn("vmm: cannot protect with invalid vmm or size 0\n");
        return -EINVAL;
    }

    base = ALIGN_DOWN(base, PAGE_SIZE);
    size = ALIGN_UP(size, PAGE_SIZE);

    spinlock_acquire(&vmm_lock);

    vaddr_t addr = base;
    vaddr_t end = base + size;

    while (addr < end) {
        vma_t *vma = _find_vma(vmm, addr);

        if (!vma) {
            warn("vmm: no vma found for protect at addr=0x%zx\n", addr);
            spinlock_release(&vmm_lock);
            return -EINVAL;
        }

        if (vma->base < addr) {
            if (!_vma_split(vmm, vma, addr)) {
                warn("vmm: failed to split vma during protect at addr=0x%zx\n", addr);
                spinlock_release(&vmm_lock);
                return -ENOMEM;
            }
            vma = _find_vma(vmm, addr);
        }

        if (vma->base + vma->size > end) {
            if (!_vma_split(vmm, vma, end)) {
                warn("vmm: failed to split vma during protect at addr=0x%zx\n", addr);
                spinlock_release(&vmm_lock);
                return -ENOMEM;
            }
        }

        uint32_t preserve = VFLAG_ALLOCATED | VFLAG_LAZY | VFLAG_PRESENT;
        vma->flags = (vma->flags & preserve) | (new_flags & ~preserve);

        if (vma->flags & VFLAG_ALLOCATED) {
            uint64_t pflags = _vflags_to_pflags(vma->flags);
            size_t npages = vma->size / PAGE_SIZE;

            for (size_t i = 0; i < npages; i++) {
                vaddr_t vaddr = vma->base + i * PAGE_SIZE;
                uint64_t paddr = get_paddr((pte_t *)vmm->pml4, vaddr);

                if (paddr) {
                    page_t *page = pfn_db_phys_to_page(paddr);
                    if (page) {
                        unmap_page(vmm->pml4, vaddr);
                        map_page(vmm->pml4, vaddr, page, pflags);
                    } else {
                        warn("vmm: invalid page for protect at vaddr=0x%zx paddr=0x%zx\n", vaddr, paddr);
                    }
                } else {
                    warn("vmm: no physical address for protect at vaddr=0x%zx\n", vaddr);
                }
            }
        }

        addr = vma->base + vma->size;
    }

    spinlock_release(&vmm_lock);
    return 0;
}

vaddr_t vmm_brk(vmm_t *vmm, vaddr_t new_brk) {
    if (!vmm) {
        warn("vmm: cannot brk with invalid vmm\n");
        return 0;
    }

    new_brk = ALIGN_UP(new_brk, PAGE_SIZE);

    if (new_brk < vmm->brk_base) {
        warn("vmm: new_brk=0x%zx is below brk_base=0x%zx\n", new_brk, vmm->brk_base);
        return vmm->brk_base;
    }

    spinlock_acquire(&vmm_lock);

    if (new_brk > vmm->brk) {
        size_t grow = new_brk - vmm->brk;
        uint32_t flags = VFLAG_PRESENT | VFLAG_WRITABLE | VFLAG_USER;
        vaddr_t got = vmm_map(vmm, vmm->brk, grow, flags);

        if (!got) {
            return vmm->brk;
        }

        vmm->brk = new_brk;
        return new_brk;
    } else if (new_brk < vmm->brk) {
        size_t shrink = vmm->brk - new_brk;

        if (vmm_unmap(vmm, new_brk, shrink) != 0) {
            warn("vmm: failed to unmap during brk shrink at new_brk=0x%zx\n", new_brk);
            return vmm->brk;
        }

        vmm->brk = new_brk;
        return new_brk;
    }

    spinlock_release(&vmm_lock);
    return vmm->brk;
}

vma_t *vmm_find(vmm_t *vmm, vaddr_t addr) {
    if (!vmm) {
        return NULL;
    }

    return _find_vma(vmm, addr);
}
 
vma_t *vmm_find_next(vmm_t *vmm, vaddr_t addr) {
    if (!vmm) {
        return NULL;
    }

    return _find_vma_next(vmm, addr);
}
 
int vmm_is_accessible(vmm_t *vmm, vaddr_t base, size_t size, uint32_t access) {
    if (!vmm || size == 0) {
        return 0;
    }
 
    vaddr_t addr = ALIGN_DOWN(base, PAGE_SIZE);
    vaddr_t end  = ALIGN_UP(base + size, PAGE_SIZE);
 
    while (addr < end) {
        vma_t *v = _find_vma(vmm, addr);

        if (!v) {
            return 0;
        }

        if ((v->flags & access) != access) {
            return 0;
        }

        addr = v->base + v->size;
    }

    return 1;
}

int vmm_pf_handler(vmm_t *vmm, vaddr_t fault_addr, uint64_t error_code) {
    if (!vmm) {
        return -EINVAL;
    }

    vaddr_t fault_page = ALIGN_DOWN(fault_addr, PAGE_SIZE);
    vma_t *vma = _find_vma(vmm, fault_page);

    if (!vma) {
        warn("vmm: page fault at addr=0x%zx with no corresponding vma (error_code=0x%lx)\n", fault_addr, error_code);
        return -EFAULT;
    }

    int is_write = !!(error_code & PF_WRITE);
    int is_user = !!(error_code & PF_USER);
    int is_ifetch = !!(error_code & PF_IFETCH);

    if (is_write && !(vma->flags & VFLAG_WRITABLE)) {
        warn("vmm: page fault at addr=0x%zx due to write access to non-writable vma (error_code=0x%lx)\n", fault_addr, error_code);
        return -EFAULT;
    }

    if (is_user && !(vma->flags & VFLAG_USER)) {
        warn("vmm: page fault at addr=0x%zx due to user access to non-user vma (error_code=0x%lx)\n", fault_addr, error_code);
        return -EFAULT;
    }

    if (is_ifetch && !(vma->flags & VFLAG_EXECUTABLE)) {
        warn("vmm: page fault at addr=0x%zx due to instruction fetch from non-executable vma (error_code=0x%lx)\n", fault_addr, error_code);
        return -EFAULT;
    }

    uint64_t paddr = get_paddr((pte_t *)vmm->pml4, fault_page);

    if (!paddr) {
        if (!(vma->flags & VFLAG_LAZY)) {
            warn("vmm: page fault at addr=0x%zx with vma that is not lazy but has no physical page (error_code=0x%lx)\n", fault_addr, error_code);
            return -EFAULT;
        }

        page_t *page = pmm_alloc_page();
        if (!page) {
            warn("vmm: failed to allocate page for lazy page fault at addr=0x%zx (error_code=0x%lx)\n", fault_addr, error_code);
            return -ENOMEM;
        }

        uint64_t phys = pfn_db_page_to_phys(page);
        memset(PHYS_TO_VIRT(phys), 0, PAGE_SIZE);

    }

    if (is_write && (error_code & PF_PRESENT)) {
        page_t *old_page = pfn_db_phys_to_page(paddr);
        if (!old_page || !is_page_cow(old_page)) {
            warn("vmm: page fault at addr=0x%zx due to write access to non-COW page (error_code=0x%lx)\n", fault_addr, error_code);
            return -EFAULT;
        }

        if (old_page->u2.sharecount <= 1) {
            pmm_page_clear_cow(old_page);
            
            uint64_t pflags = _vflags_to_pflags(vma->flags);
            map_page(vmm->pml4, fault_page, old_page, pflags);
            return EOK;
        }

        page_t *new_page = pmm_alloc_page();
        if (!new_page) {
            warn("vmm: failed to allocate page for COW fault at addr=0x%zx (error_code=0x%lx)\n", fault_addr, error_code);
            return -ENOMEM;
        }

        uint64_t new_phys = pfn_db_page_to_phys(new_page);
        memset(PHYS_TO_VIRT(new_phys), 0, PAGE_SIZE);
        memcpy(PHYS_TO_VIRT(new_phys), PHYS_TO_VIRT(paddr), PAGE_SIZE);

        pmm_page_unshare(old_page);
        pmm_page_release(old_page);

        map_page(vmm->pml4, fault_page, new_page, _vflags_to_pflags(vma->flags));
        return EOK;
    }

    warn("vmm: unhandled page fault at addr=0x%zx with error_code=0x%lx\n", fault_addr, error_code);
    return EFAULT;
}

vmm_t* vmm_fork(vmm_t *parent) {
    if (!parent) {
        return NULL;
    }

    vmm_t *child = vmm_create();
    if (!child) {
        return NULL;
    }

    spinlock_acquire(&vmm_lock);

    rb_node_t *node;
    rb_for_each(node, &parent->vmas) {
        vma_t *parent_vma = containerof(node, vma_t, rbn);
        vma_t *child_vma = _vma_alloc();

        if (!child_vma) {
            vmm_destroy(child);
            spinlock_release(&vmm_lock);
            return NULL;
        }

        child_vma->base = parent_vma->base;
        child_vma->size = parent_vma->size;
        child_vma->flags = parent_vma->flags;
        child_vma->vnode_backing = parent_vma->vnode_backing;
        child_vma->vnode_offset = parent_vma->vnode_offset;

        if (parent_vma->flags & VFLAG_SHARED) {
            size_t npages = parent_vma->size / PAGE_SIZE;

            for (size_t i = 0; i < npages; i++) {
                vaddr_t vaddr = parent_vma->base + i * PAGE_SIZE;
                uint64_t paddr = get_paddr((pte_t *)parent->pml4, vaddr);

                if (!paddr) {
                    continue;
                }

                page_t *page = pfn_db_phys_to_page(paddr);
                if (!page) {
                    continue;
                }

                pmm_page_share(page);
                pmm_page_retain(page);

                map_page(child->pml4, vaddr, page, _vflags_to_pflags(parent_vma->flags));
            }
        } else if (parent_vma->flags & VFLAG_ALLOCATED) {
            uint64_t ro_pflags = _vflags_to_pflags(parent_vma->flags) & ~PFLAG_WRITE;

            size_t npages = parent_vma->size / PAGE_SIZE;

            for (size_t i = 0; i < npages; i++) {
                vaddr_t vaddr = parent_vma->base + i * PAGE_SIZE;
                uint64_t paddr = get_paddr((pte_t *)parent->pml4, vaddr);

                if (!paddr) {
                    continue;
                }

                page_t *page = pfn_db_phys_to_page(paddr);
                if (!page) {
                    continue;
                }

                pmm_page_mark_cow(page);
                pmm_page_share(page);
                pmm_page_retain(page);

                map_page(child->pml4, vaddr, page, ro_pflags);
                map_page(parent->pml4, vaddr, page, ro_pflags);
            }
        }

        rb_insert(&child->vmas, &child_vma->rbn);
        child->vmas_count++;
    }

    child->mmap_base = parent->mmap_base;
    child->stack_top = parent->stack_top;
    child->brk = parent->brk;
    child->brk_base = parent->brk_base;
 
    spinlock_release(&vmm_lock);
    return child;
}

vmm_t *vmm_create_from_pml4(uint64_t *pml4) {
    vmm_t *vmm = kzalloc(sizeof(vmm_t));
    if (!vmm) {
        return NULL;
    }

    vmm->vmas = (rb_tree_t)RB_TREE_INIT(_vma_cmp);
    vmm->pml4 = pml4;
    vmm->mmap_base = VMM_MMAP_BASE;
    vmm->stack_top = VMM_STACK_TOP;

    return vmm;
}

void vmm_dump(vmm_t *vmm) {
    if (!vmm) {
        debug("vmm: (null)\n");
        return;
    }
 
    debug("vmm: pml4=0x%llx vmas=%zu brk=0x%lx mmap_base=0x%lx\n",
          (unsigned long long)vmm->pml4,
          vmm->vmas_count,
          vmm->brk,
          vmm->mmap_base);
 
    rb_node_t *node;
    rb_for_each(node, &vmm->vmas) {
        vma_t *v = containerof(node, vma_t, rbn);
 
        char r = (v->flags & VFLAG_PRESENT)    ? 'p' : '-';
        char w = (v->flags & VFLAG_WRITABLE)   ? 'w' : '-';
        char x = (v->flags & VFLAG_EXECUTABLE) ? 'x' : '-';
        char u = (v->flags & VFLAG_USER)        ? 'u' : 'k';
        char l = (v->flags & VFLAG_LAZY)        ? 'L' : '-';
        char s = (v->flags & VFLAG_SHARED)      ? 'S' : '-';
        char c = (v->flags & VFLAG_STACK)       ? 'T' : '-';
 
        debug("  [0x%016lx - 0x%016lx]  %c%c%c%c%c%c%c  size=0x%zx\n",
              v->base, v->base + v->size,
              r, w, x, u, l, s, c,
              v->size);
    }
}
