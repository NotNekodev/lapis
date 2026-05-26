#include "uacpi/event.h"
#include "uacpi/status.h"
#include "uacpi/uacpi.h"
#include <arch/gdt/gdt.h>
#include <arch/interrupts/idt.h>
#include <arch/interrupts/isr.h>
#include <arch/io.h>
#include <arch/cpu.h>
#include <arch/smp.h>

#include <mm/pfn_db.h>
#include <mm/pmm.h>
#include <mm/kheap.h>
#include <mm/paging.h>

#include <kernel.h>

#include <log/sinks/e9.h>
#include <log/log.h>

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_executable_address_request executable_address_request = {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID,
    .revision = 0
};

__attribute((used, section(".limine_requests")))
static volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

kernel_info_t kernel_info;

void kmain(void) {
    __asm__ volatile("movq %%rsp, %0" : "=r"(kernel_info.kstack_top));

    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
        hcf();
    }

    if (framebuffer_request.response == NULL
     || framebuffer_request.response->framebuffer_count < 1) {
        hcf();
    }

    kernel_info.framebuffer = framebuffer_request.response->framebuffers[0];
    kernel_info.hhdm_offset = hhdm_request.response->offset;
    kernel_info.memmap = memmap_request.response;
    kernel_info.kaddr_virt = executable_address_request.response->virtual_base;
    kernel_info.kaddr_phys = executable_address_request.response->physical_base;
    kernel_info.rsdp_addr = (uint64_t)(uintptr_t)rsdp_request.response->address;

    e9_sink_init();

    smp_prepare();

    gdt_reload();

    idt_setup();
    idt_reload();

    smp_start_aps();

    pfn_db_init(kernel_info.memmap);
    pfn_db_dump();

    pmm_init();
    pmm_dump_stats();

    kheap_init();
    paging_init();

    vmm_t *kvm = vmm_create_from_pml4(kernel_info.kernel_pt);
    vmm_set_kernel(kvm);

    register_interrupt(0xE, pf_handler, NULL);

    vmm_dump(vmm_current());

    uacpi_status ret = uacpi_initialize(0);
    if (uacpi_unlikely_error(ret)) {
        critical("acpi: initial initialization of uACPI failed: %s\n", uacpi_status_to_string(ret));
        hcf();
    }

    ret = uacpi_namespace_load();
    if (uacpi_unlikely_error(ret)) {
        critical("acpi: acpi namespace initialization failed: %s\n", uacpi_status_to_string(ret));
    }

    ret = uacpi_namespace_initialize();
    if (uacpi_unlikely_error(ret)) {
        critical("acpi: acpi namespace initialization failed: %s\n", uacpi_status_to_string(ret));
        hcf();
    }

    ret = uacpi_finalize_gpe_initialization();
    if (uacpi_unlikely_error(ret)) {
        critical("acpi: failed to finalize GPE initialization: %s\n", uacpi_status_to_string(ret));
        hcf();
    }

    info("acpi: initialization complete\n");

    hcf();
}