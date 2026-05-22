#include "arch/gdt/gdt.h"
#include "arch/interrupts/idt.h"
#include "mm/pfn_db.h"
#include "mm/pmm.h"
#include <arch/smp.h>
#include <kernel.h>

#include <arch/io.h>
#include <log/sinks/e9.h>
#include <log/log.h>

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

#include <arch/cpu.h>

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

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

kernel_info_t kernel_info;

void kmain(void) {
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

    e9_sink_init();

    smp_prepare();

    debug("Hello, world! This is a debug message.\n");
    info("Hello, world! This is an info message.\n");
    warn("Hello, world! This is a warning message.\n");
    error("Hello, world! This is an error message.\n");
    critical("Hello, world! This is a critical message.\n");

    gdt_reload();
    info("GDT init... ok\n");

    idt_setup();
    idt_reload();
    info("IDT init... ok\n");

    smp_start_aps();

    pfn_db_init(kernel_info.memmap);
    pfn_db_dump();

    pmm_init();
    pmm_dump_stats();
    pmm_stress_test();

    __asm__ volatile("int $0x3");
    
    hcf();
}

