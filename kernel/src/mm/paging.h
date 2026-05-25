#include "arch/interrupts/isr.h"
#include "mm/page.h"
#include <stdint.h>
#ifndef _PAGING_H
#define _PAGING_H 1

typedef uint64_t pte_t;

#define PFLAG_PRESENT   (1ULL << 0)
#define PFLAG_WRITE     (1ULL << 1)
#define PFLAG_USER      (1ULL << 2)
#define PFLAG_PWT       (1ULL << 3)
#define PFLAG_PCD       (1ULL << 4)
#define PFLAG_ACCESSED  (1ULL << 5)
#define PFLAG_DIRTY     (1ULL << 6)
#define PFLAG_HUGE      (1ULL << 7)
#define PFLAG_GLOBAL    (1ULL << 8)
#define PFLAG_NX        (1ULL << 63)

#define PFLAG_RW        (PFLAG_PRESENT | PFLAG_WRITE)
#define PFLAG_RO        (PFLAG_PRESENT)
#define PFLAG_URW       (PFLAG_PRESENT | PFLAG_WRITE | PFLAG_USER)
#define PFLAG_URO       (PFLAG_PRESENT | PFLAG_USER)
#define PFLAG_MMIO      (PFLAG_PRESENT | PFLAG_WRITE | PFLAG_PCD | PFLAG_PWT | PFLAG_NX)

// x86 page fault error code bits
#define PF_PRESENT (1 << 0)
#define PF_WRITE   (1 << 1)
#define PF_USER    (1 << 2)
#define PF_IFETCH  (1 << 3)

void map_page(pte_t *pt, uint64_t vaddr, page_t *page, uint64_t flags);
void map_paddr(pte_t *pt, uint64_t vaddr, uint64_t paddr, uint64_t flags);
void map_mmio(pte_t *pt, uint64_t vaddr, uint64_t paddr, uint64_t npages);

void unmap_page(pte_t *pt, uint64_t vaddr);

uint64_t get_paddr(pte_t *pt, uint64_t vaddr);
uint64_t get_pflags(pte_t *pt, uint64_t vaddr);

pte_t *pte_alloc(void);
void pte_free(pte_t *pt);

void pte_free_empty(pte_t *pt, uint64_t vaddr);
void reg_kernel_mmio(uint64_t vaddr, uint64_t paddr, uint64_t npages);

void paging_init(void);

void pf_handler(isr_t *self, context_t *ctx);

#endif // _PAGING_H