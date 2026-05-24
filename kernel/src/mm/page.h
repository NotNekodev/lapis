#ifndef _PAGE_H
#define _PAGE_H

#include <stdint.h>
#include <kernel.h>

#define PAGE_SIZE 0x1000

#define PAGE_FREE       (1u << 0)
#define PAGE_ALLOCATED  (1u << 1)
#define PAGE_RESERVED   (1u << 2)
#define PAGE_SHARED     (1u << 3)
#define PAGE_COW        (1u << 4)
#define PAGE_DIRTY      (1u << 5) // use after free, dont realloc

typedef struct page {
    union {
        struct page* next;
    } u1;

    union {
		struct page *prev;
		uint64_t sharecount;
	} u2;

    uint64_t flags;
	uint32_t refcount;
} __attribute__((aligned(64))) page_t;

#define PHYS_TO_VIRT(p) ((void *)((uint64_t)(p) + kernel_info.hhdm_offset))
#define VIRT_TO_PHYS(p) ((uint64_t)(p) - kernel_info.hhdm_offset)

#define is_page_free(p)         ((p)->flags & PAGE_FREE)
#define is_page_allocated(p)    ((p)->flags & PAGE_ALLOCATED)
#define is_page_reserved(p)     ((p)->flags & PAGE_RESERVED)
#define is_page_shared(p)       ((p)->flags & PAGE_SHARED)
#define is_page_cow(p)          ((p)->flags & PAGE_COW)
#define is_page_dirty(p)        ((p)->flags & PAGE_DIRTY)

#define DIV_ROUND_UP(x, y)  (((uint64_t)(x) + ((uint64_t)(y) - 1)) / (uint64_t)(y))
#define ALIGN_UP(x, y)      (DIV_ROUND_UP(x, y) * (uint64_t)(y))
#define ALIGN_DOWN(x, y)    (((uint64_t)(x) / (uint64_t)(y)) * (uint64_t)(y))


#endif // _PAGE_H