#ifndef _KERNEL_H
#define _KERNEL_H 1

#include <stdint.h>

#include <mm/vmm.h>

#include <uacpi/acpi.h>

#define KSTACK_SIZE 64 * 0x1000
#define MAX_IOAPICS 8
#define MAX_ISO 16

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

    struct {
        size_t ioapic_count;
        struct ioapic_entry {
            uint8_t  id;
            uint32_t gsi_base;
            uintptr_t phys_addr;
        } ioapics[MAX_IOAPICS];
        struct {
            uint8_t source;
            uint32_t gsi;
            uint16_t flags;
        } iso_table[MAX_ISO];
        size_t iso_count;
    } ioapic;

    uint64_t tsc_freq;
} kernel_info_t;

extern kernel_info_t kernel_info;

#endif // _KERNEL_H