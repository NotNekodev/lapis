#include "paging.h"
#include <mm/pmm.h>
#include <stdint.h>
#include <util/memory.h>
#include "kernel.h"
#include "log/log.h"
#include "mm/page.h"
#include "mm/pfn_db.h"
#include "mm/vmm.h"
#include "stddef.h"
#include "util/errno.h"
#include "util/spinlock.h"
#include <mm/kheap.h>

extern char __limine_requests_start[];
extern char __limine_requests_end[];
extern char __text_start[];
extern char __text_end[];
extern char __rodata_start[];
extern char __rodata_end[];
extern char __data_start[];
extern char __data_end[];

#define PAGE_FRAME_MASK 0x000FFFFFFFFFF000ULL
#define PML_IDX_MASK 0x1ffULL
#define PML_SHIFT_L1 12
#define PML_SHIFT_L2 21
#define PML_SHIFT_L3 30
#define PML_SHIFT_L4 39

static spinlock_t paging_lock = SPINLOCK_INIT("paging_lock");

typedef struct mmio_region {
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t npages;
    struct mmio_region *next;
} mmio_region_t;

static spinlock_t mmio_lock = SPINLOCK_INIT("mmio_lock");
static mmio_region_t *mmio_regions = NULL;

static inline uint16_t _pml1_index(uintptr_t v) {
	return (v >> PML_SHIFT_L1) & PML_IDX_MASK;
}

static inline uint16_t _pml2_index(uintptr_t v) {
	return (v >> PML_SHIFT_L2) & PML_IDX_MASK;
}

static inline uint16_t _pml3_index(uintptr_t v) {
	return (v >> PML_SHIFT_L3) & PML_IDX_MASK;
}

static inline uint16_t _pml4_index(uintptr_t v) {
	return (v >> PML_SHIFT_L4) & PML_IDX_MASK;
}


static uint64_t _alloc_pte(void) {
    page_t *page = pmm_alloc_page();
    if (!page) {
        return 0;
    }

    uint64_t phys = pfn_db_page_to_phys(page);
    memset(PHYS_TO_VIRT(phys), 0, PAGE_SIZE);

    return phys;
}

static void _free_pte(uint64_t phys) {
    page_t *page = pfn_db_phys_to_page(phys);
    if (!page) {
        critical("paging: invalid page to free paddr=%#018llx\n", phys);
        return;
    }

    pmm_page_release(page);
}

static uint64_t* _pte_next(uint64_t *table, uint64_t index, uint64_t flags) {
    uint64_t entry = table[index];

    if (entry & PFLAG_PRESENT) {
        return (uint64_t *)(PHYS_TO_VIRT(entry & PAGE_FRAME_MASK));
    }

    if (!flags) {
        return NULL;
    }

    uint64_t phys = _alloc_pte();
    if (!phys) {
        return NULL;
    }

    table[index] = phys | flags;
    return (uint64_t *)(PHYS_TO_VIRT(phys));
}

static uint64_t* _leaf_entry(uint64_t *pml4, uint64_t vaddr, uint64_t flags) {
    uint64_t *pml3 = _pte_next(pml4, _pml4_index(vaddr), flags);
    if (!pml3) {
        return NULL;
    }

    uint64_t *pml2 = _pte_next(pml3, _pml3_index(vaddr), flags);
    if (!pml2) {
        return NULL;
    }

    uint64_t *pml1 = _pte_next(pml2, _pml2_index(vaddr), flags);
    if (!pml1) {
        return NULL;
    }

    return &pml1[_pml1_index(vaddr)];
}

static void _pte_free_level(uint64_t *table, uint64_t depth) {
    if (depth == 1) {
        for (int i = 0; i < 512; i++) {
            uint64_t entry = table[i];
            if (!(entry & PFLAG_PRESENT)) {
                continue;
            }

            uint64_t phys = entry & PAGE_FRAME_MASK;
            page_t *page = pfn_db_phys_to_page(phys);
            if (!page) {
                warn("paging: invalid page to free paddr=%#018llx\n", phys);
                continue;
            }

            pmm_page_unshare(page);
            pmm_page_release(page);
        }

        _free_pte(VIRT_TO_PHYS(table));
        return;
    }

    for (int i = 0; i < 512; i++) {
        uint64_t entry = table[i];

        if (!(entry & PFLAG_PRESENT)) {
            continue;
        }

        if (entry & PFLAG_HUGE) {
            warn("paging: huge page entry found during free, ignoring\n");
            continue;
        }

        uint64_t *next_table = (uint64_t *)(PHYS_TO_VIRT(entry & PAGE_FRAME_MASK));
        _pte_free_level(next_table, depth - 1);
    }

    _free_pte(VIRT_TO_PHYS(table));
}

static int _table_empty(uint64_t *table) {
    for (int i = 0; i < 512; i++) {
        if (table[i] & PFLAG_PRESENT) {
            return 0;
        }
    }
    return 1;
}

static void __u_pte_free_empty(pte_t *pt, uint64_t vaddr) {
    uint64_t *pml4 = (uint64_t *)PHYS_TO_VIRT((uint64_t)pt);

	uint64_t i4 = _pml4_index(vaddr);
	if (!(pml4[i4] & PFLAG_PRESENT))
		return;
	uint64_t *pml3 = (uint64_t *)PHYS_TO_VIRT(pml4[i4] & PAGE_FRAME_MASK);

	uint64_t i3 = _pml3_index(vaddr);
	if (!(pml3[i3] & PFLAG_PRESENT))
		return;
	uint64_t *pml2 = (uint64_t *)PHYS_TO_VIRT(pml3[i3] & PAGE_FRAME_MASK);

	uint64_t i2 = _pml2_index(vaddr);
	if (!(pml2[i2] & PFLAG_PRESENT))
		return;
	uint64_t *pml1 = (uint64_t *)PHYS_TO_VIRT(pml2[i2] & PAGE_FRAME_MASK);

	if (_table_empty(pml1)) {
		_free_pte(pml2[i2] & PAGE_FRAME_MASK);
		pml2[i2] = 0;
	} else
		return;

	if (_table_empty(pml2)) {
		_free_pte(pml3[i3] & PAGE_FRAME_MASK);
		pml3[i3] = 0;
	} else
		return;

	if (_table_empty(pml3)) {
		_free_pte(pml4[i4] & PAGE_FRAME_MASK);
		pml4[i4] = 0;
	}
}

void map_page(pte_t *pt, uintptr_t vaddr, page_t* page, uint64_t flags) {
    // TODO: replace with assert
    if (!page) {
        critical("paging: cannot map page, page is NULL\n");
        return;
    }

    // TODO: replace with assert
    if (!pt) {
        critical("paging: cannot map page, page table is NULL\n");
        return;
    }

    spinlock_acquire(&paging_lock);

    vaddr = ALIGN_DOWN(vaddr, PAGE_SIZE);

    uint64_t phys = pfn_db_page_to_phys(page);
    uint64_t *pml4 = (uint64_t *)PHYS_TO_VIRT((uint64_t)pt);
    uint64_t table_flags = PFLAG_PRESENT | PFLAG_WRITE;

    if (flags & PFLAG_USER) {
        table_flags |= PFLAG_USER;
    }

    uint64_t *entry = _leaf_entry(pml4, vaddr, table_flags);
    if (!entry) {
        critical("paging: failed to allocate page table for mapping\n");
        spinlock_release(&paging_lock);
        return;
    }

    if (*entry & PFLAG_PRESENT) {
        warn("paging: mapping page to vaddr=%#018llx which is already mapped, overwriting\n", vaddr);

        uint64_t old_phys = *entry & PAGE_FRAME_MASK;
        page_t *old_page = pfn_db_phys_to_page(old_phys);

        if (old_page) {
            pmm_page_unshare(old_page);
            pmm_page_release(old_page);
        } else {
            warn("paging: invalid page to free paddr=%#018llx\n", old_phys);
        }
    }

    *entry = phys | (flags & ~PAGE_FRAME_MASK) | PFLAG_PRESENT;

    pmm_page_share(page);
    pmm_page_retain(page);

    __asm__ volatile("invlpg (%0)" ::"r"(vaddr) : "memory");
    spinlock_release(&paging_lock);
}

void map_paddr(pte_t *pt, uintptr_t vaddr, uint64_t paddr, uint64_t flags) {
    if (!pt) {
        critical("paging: cannot map physical address, page table is NULL\n");
        return;
    }

    spinlock_acquire(&paging_lock);

    vaddr = ALIGN_DOWN(vaddr, PAGE_SIZE);
    paddr = ALIGN_DOWN(paddr, PAGE_SIZE);

    uint64_t *pml4 = (uint64_t *)PHYS_TO_VIRT((uint64_t)pt);
    uint64_t table_flags = PFLAG_PRESENT | PFLAG_WRITE;

    if (flags & PFLAG_USER) {
        table_flags |= PFLAG_USER;
    }

    uint64_t *entry = _leaf_entry(pml4, vaddr, table_flags);
    if (!entry) {
        critical("paging: failed to allocate page table for mapping\n");
        return;
    }

    if (*entry & PFLAG_PRESENT) {
        warn("paging: mapping physical address to vaddr=%#018llx which is already mapped, overwriting\n", vaddr);

        uint64_t old_phys = *entry & PAGE_FRAME_MASK;
        page_t *old_page = pfn_db_phys_to_page(old_phys);

        if (old_page) {
            pmm_page_unshare(old_page);
            pmm_page_release(old_page);
        } else {
            warn("paging: invalid page to free paddr=%#018llx\n", old_phys);
        }
    }

    *entry = paddr | (flags & ~PAGE_FRAME_MASK) | PFLAG_PRESENT;

    __asm__ volatile("invlpg (%0)" ::"r"(vaddr) : "memory");

    spinlock_release(&paging_lock);
}

void unmap_page(pte_t *pt, uint64_t vaddr)  {
    if (!pt) {
        critical("paging: cannot unmap page, page table is NULL\n");
        return;
    }

    spinlock_acquire(&paging_lock);

    vaddr = ALIGN_DOWN(vaddr, PAGE_SIZE);

    uint64_t *pml4 = (uint64_t *)PHYS_TO_VIRT((uint64_t)pt);
    uint64_t *entry = _leaf_entry(pml4, vaddr, 0);

    if (!entry || !(*entry & PFLAG_PRESENT)) {
        warn("paging: cannot unmap page at vaddr=%#018llx, not mapped\n", vaddr);
        spinlock_release(&paging_lock);
        return;
    }

    uint64_t phys = *entry & PAGE_FRAME_MASK;
    page_t *page = pfn_db_phys_to_page(phys);
    if (!page) {
        warn("paging: invalid page to free paddr=%#018llx\n", phys);
    } else {
        pmm_page_unshare(page);
        pmm_page_release(page);
    }

    *entry = 0;
    __asm__ volatile("invlpg (%0)" ::"r"(vaddr) : "memory");

    __u_pte_free_empty(pt, vaddr);

    spinlock_release(&paging_lock);
}

uint64_t get_paddr(pte_t *pt, uint64_t vaddr) {
    if (!pt) {
        critical("paging: cannot get physical address, page table is NULL\n");
        return 0;
    }

    uint64_t *pml4 = (uint64_t *)PHYS_TO_VIRT((uint64_t)pt);
    uint64_t *entry = _leaf_entry(pml4, vaddr, 0);

    if (!entry || !(*entry & PFLAG_PRESENT)) {
        return 0;
    }

    return *entry & PAGE_FRAME_MASK;
}

uint64_t get_pflags(pte_t *pt, uint64_t vaddr) {
    if (!pt) {
        critical("paging: cannot get page flags, page table is NULL\n");
        return 0;
    }

    uint64_t *pml4 = (uint64_t *)PHYS_TO_VIRT((uint64_t)pt);
    uint64_t *entry = _leaf_entry(pml4, vaddr, 0);

    if (!entry || !(*entry & PFLAG_PRESENT)) {
        return 0;
    }

    return *entry & ~PAGE_FRAME_MASK;
}

pte_t* pte_alloc(void) {
    page_t *page = pmm_alloc_page();
	if (!page) {
        critical("paging: failed to allocate page for page table\n");
        return NULL;
    }

	uint64_t phys = pfn_db_page_to_phys(page);
    uint64_t *pml4 = PHYS_TO_VIRT(phys);
	memset(pml4, 0, PAGE_SIZE);
	if (kernel_info.kernel_pt) {
		uint64_t *kpml4 = PHYS_TO_VIRT((uint64_t)kernel_info.kernel_pt);

		for (int i = 256; i < 512; i++) {
			pml4[i] = kpml4[i];
        }
	}

	spinlock_acquire(&mmio_lock);

	for (mmio_region_t *region = mmio_regions; region; region = region->next) {
		map_mmio((pte_t *)phys, region->vaddr, region->paddr, region->npages);
    }

	spinlock_release(&mmio_lock);
	return (pte_t *)phys;
}

void pte_free(pte_t *pt) {
    if (!pt) {
        critical("paging: cannot free page table, page table pointer is NULL\n");
        return;
    }

    uint64_t *pml4 = (uint64_t *)PHYS_TO_VIRT((uint64_t)pt);

    if (pt == kernel_info.kernel_pt) {
        critical("paging: cannot free kernel page table\n");
        return;
    }

    for (int i = 0; i < 256; i++) {
        uint64_t entry = pml4[i];

        if (!(entry & PFLAG_PRESENT)) {
            continue;
        }

        if (entry & PFLAG_HUGE) {
            warn("paging: huge page entry found during free, ignoring\n");
            continue;
        }

        uint64_t *next_table = (uint64_t *)(PHYS_TO_VIRT(entry & PAGE_FRAME_MASK));
        _pte_free_level(next_table, 3);
    }

    _free_pte((uint64_t)pt);
}

void pte_free_empty(pte_t *pt, uint64_t vaddr) {
    spinlock_acquire(&paging_lock);
    __u_pte_free_empty(pt, vaddr);
    spinlock_release(&paging_lock);
}

void map_mmio(pte_t *pt, uint64_t vaddr, uint64_t paddr, uint64_t npages) {
    if (!pt) {
        critical("paging: cannot map MMIO region, page table is NULL\n");
        return;
    }

    vaddr = ALIGN_DOWN(vaddr, PAGE_SIZE);
	paddr = ALIGN_DOWN(paddr, PAGE_SIZE);

	uint64_t *pml4 = (uint64_t *)PHYS_TO_VIRT((uint64_t)pt);

	for (uint64_t i = 0; i < npages; i++) {
		uint64_t v = vaddr + i * PAGE_SIZE;
		uint64_t p = paddr + i * PAGE_SIZE;

		uint64_t *pte = _leaf_entry(pml4, v, PFLAG_PRESENT | PFLAG_WRITE);
		if (!pte) {
			critical("paging: failed to allocate page table for MMIO mapping\n");
            return;
		}

		*pte = p | PFLAG_MMIO;

		__asm__ volatile("invlpg (%0)" ::"r"(v) : "memory");
	}
}

void reg_kernel_mmio(uint64_t vaddr, uint64_t paddr, uint64_t npages) {
    spinlock_acquire(&mmio_lock);

    mmio_region_t *region = kzalloc(sizeof(mmio_region_t));
    if (!region) {
        critical("paging: failed to allocate MMIO region\n");
        spinlock_release(&mmio_lock);
        return;
    }

    region->vaddr = vaddr;
    region->paddr = paddr;
    region->npages = npages;
    region->next = mmio_regions;
    mmio_regions = region;

    spinlock_release(&mmio_lock);
}

static inline void _map_section(uint64_t vstart, uint64_t vend, uint64_t flags) {
    uint64_t vaddr = ALIGN_DOWN(vstart, PAGE_SIZE);
    uint64_t end = ALIGN_UP(vend, PAGE_SIZE);

    for (; vaddr < end; vaddr += PAGE_SIZE) {
        uint64_t phys = vaddr - kernel_info.kaddr_virt + kernel_info.kaddr_phys;
        map_paddr(kernel_info.kernel_pt, vaddr, phys, flags);
    }
}

void paging_init(void) {
    kernel_info.kernel_pt = pte_alloc();
    if (!kernel_info.kernel_pt) {
        critical("paging: failed to allocate kernel page table\n");
        return;
    }

    _map_section((uint64_t)__limine_requests_start, (uint64_t)__limine_requests_end, PFLAG_RO | PFLAG_NX);
    _map_section((uint64_t)__text_start, (uint64_t)__text_end, PFLAG_RO);
    _map_section((uint64_t)__rodata_start, (uint64_t)__rodata_end, PFLAG_RO | PFLAG_NX);
    _map_section((uint64_t)__data_start, (uint64_t)__data_end, PFLAG_RW | PFLAG_NX);

    uint64_t stack_top = ALIGN_UP(kernel_info.kstack_top, PAGE_SIZE);
    uint64_t stack_start = stack_top - KSTACK_SIZE;

    for (uint64_t vaddr = stack_start; vaddr < stack_top; vaddr += PAGE_SIZE) {
        uint64_t phys = vaddr - kernel_info.kaddr_virt + kernel_info.kaddr_phys;
        map_paddr(kernel_info.kernel_pt, vaddr, phys, PFLAG_RW);
    }

    uint64_t *new_pml4 = (uint64_t *)PHYS_TO_VIRT((uint64_t)kernel_info.kernel_pt);

    uint64_t boot_cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(boot_cr3));
    uint64_t *boot_pml4 = (uint64_t *)PHYS_TO_VIRT(boot_cr3);
    for (int i = 256; i < 512; i++) {
        new_pml4[i] = boot_pml4[i];
    }

    __asm__ volatile("mov %0, %%cr3" ::"r"(kernel_info.kernel_pt) : "memory");

    info("paging: initialized kernel page table at paddr=0x%.16llx\n", VIRT_TO_PHYS(kernel_info.kernel_pt));
}

void pf_handler(isr_t *self, context_t *ctx) {
    (void)self;

    vmm_t* fault_vmm = vmm_current(); // TODO: is this the correct way to get the faulting VMM? maybe once we have a scheduler we need to get the VMM from the faulting thread's context instead?
    vaddr_t fault_addr = ctx->cr2;
    uint64_t error_code = ctx->error;

    if (vmm_pf_handler(fault_vmm, fault_addr, error_code) != EOK) {
        critical("paging: unhandled page fault at addr=0x%zx with error_code=0x%lx\n", fault_addr, error_code);
        hcf();
    }
}