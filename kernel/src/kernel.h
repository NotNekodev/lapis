#ifndef _KERNEL_H
#define _KERNEL_H 1

#include <stdint.h>

#include <mm/vmm.h>

#define KSTACK_SIZE 64 * 0x1000

typedef struct kernel_info {
    struct limine_framebuffer *framebuffer;

    uint64_t hhdm_offset;
    struct limine_memmap_response *memmap;

    uint64_t *kernel_pt;
    vmm_t *kernel_vmm;

    uint64_t kstack_top;
    uint64_t kaddr_virt;
    uint64_t kaddr_phys;

    uint64_t rsdp_addr;
} kernel_info_t;

extern kernel_info_t kernel_info;

#endif // _KERNEL_H