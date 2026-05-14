#include "arch/gdt/gdt.h"
#include "arch/interrupts/idt.h"
#include <kernel.h>

#include <arch/io.h>
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

    e9_sink_init();

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

    __asm__ volatile("int $0x3");
    
    hcf();
}
