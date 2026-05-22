#ifndef _KERNEL_H
#define _KERNEL_H 1

#include "limine.h"

typedef struct kernel_info {
    struct limine_framebuffer *framebuffer;

    uint64_t hhdm_offset;
    struct limine_memmap_response *memmap;
} kernel_info_t;

extern kernel_info_t kernel_info;

#endif // _KERNEL_H