#include "arch/interrupts/apic.h"
#include "arch/interrupts/irq.h"
#include "dev/bus.h"
#include "dev/pci.h"
#include "fs/initrd/cpio.h"
#include "fs/ramfs/ramfs.h"
#include "kterm/psf.h"
#include "log/sinks/kterm.h"
#include "uacpi/event.h"
#include "uacpi/status.h"
#include "uacpi/uacpi.h"
#include "util/errno.h"
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

#include <sched/task.h>
#include <sched/sched.h>

#include <log/sinks/uart16550.h>
#include <log/sinks/e9.h>
#include <log/log.h>

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>
#include <util/memory.h>

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

__attribute__((used, section(".limine_requests")))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

kernel_info_t kernel_info;

static void apic_timer_irq(uint32_t irq, void *data, context_t *ctx) {
    (void)irq;
    (void)data;
    sched_tick(ctx);
}

static int extract_initrd(void *data, size_t size, const char *dest_path) {
    cpio_archive_t archive;
    if (cpio_archive_parse(&archive, data, size) != 0) {
        warn("Failed to parse CPIO archive\n");
        return -1;
    }

    if (cpio_archive_extract(&archive, (char *)dest_path) != 0) {
        warn("Failed to extract CPIO archive\n");
        cpio_archive_free(&archive);
        return -1;
    }

    cpio_archive_free(&archive);
    return 0;
}

static bus_t pci_bus;

static void kernel_test_thread(void *arg) {
    const char *name = (const char *)arg;
    for (;;) {
        info("kernel_test_thread(%s): iteration on cpu %ld\n", name, get_current_cpuid());
    }
    info("kernel_test_thread(%s): done\n", name);
}

extern void usermode_test_entry(void);

void int80_test(isr_t *isr, context_t *ctx) {
    (void)isr;
    (void)ctx;

    info("int80_test: called\n");
    return;
}

static void spawn_test_tasks(void) {
    proc_t *kproc_a = proc_create_kernel("test-a");
    thread_t *ta = thread_create(kproc_a, kernel_test_thread, (void *)"A", THREAD_CREATE_KERNEL);
    ta->state = THREAD_READY;
    sched_enqueue(ta);

    proc_t *kproc_b = proc_create_kernel("test-b");
    thread_t *tb = thread_create(kproc_b, kernel_test_thread, (void *)"B", THREAD_CREATE_KERNEL);
    tb->state = THREAD_READY;
    sched_enqueue(tb);

    proc_t *uproc = proc_create_kernel("test-user");
    uproc->flags |= PROC_FLAG_USER;
    uproc->vmm = vmm_create();

    vaddr_t code_page = vmm_map(uproc->vmm, 0x400000, PAGE_SIZE,
        VFLAG_PRESENT | VFLAG_WRITABLE | VFLAG_USER | VFLAG_EXECUTABLE);

    uint64_t code_phys = get_paddr((pte_t *)uproc->vmm->pml4, code_page);
    memcpy(PHYS_TO_VIRT(code_phys), (void *)usermode_test_entry, PAGE_SIZE);

    vaddr_t user_stack_top = proc_map_user_stack(uproc);

    thread_t *ut = thread_create_user(uproc, code_page, user_stack_top);
    ut->state = THREAD_READY;
    sched_enqueue(ut);
}

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

    init_bsp_cpu();

    uart_sink_init();
    e9_sink_init();

    psf_load_defaults();
    kterm_sink_init();

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

    apic_init();

    irq_init();
    int irq = irq_request_local(apic_timer_irq, NULL, "lapic-timer");

    sched_init();
    task_create_init_proc();

    smp_prepare();

    lapic_timer_init((uint8_t)irq);
    irq_enable(irq);

    _cli();

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

    pci_init(&pci_bus);

    ramfs_init();

    vfs_mount(NULL, "ramfs", "/", NULL);

    if (module_request.response && module_request.response->module_count > 0) {
        void *initrd_data = module_request.response->modules[0]->address;
        size_t initrd_size = module_request.response->modules[0]->size;

        if (extract_initrd(initrd_data, initrd_size, "/") != 0) {
            critical("Failed to extract initrd\n");
            hcf();
        }
    } else {
        critical("No initrd module provided, module response: %p\n", (void *)module_request.response);
        hcf();
    }

    vfs_mkdir("/dev", 0755);

    kfile_t *f;
    if (kopen("/test.txt", O_RDONLY, 0, &f) != EOK) {
        error("failed to open /test.txt\n");
    } else {
        char buf[128];
        size_t bytes = kread(f, buf, sizeof(buf) - 1);
        if (bytes < 0) {
            error("failed to read from /test.txt\n");
        } else {
            buf[bytes] = '\0';
            info("read from /test.txt: %s\n", buf);
        }
        kclose(f);
    }

    fs_list("/", 10);

    register_interrupt(0x80, int80_test, NULL);

    spawn_test_tasks();

    sched_start();

    sched_idle_enter();
}
