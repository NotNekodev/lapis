#include "arch/cpu.h"
#include "arch/interrupts/apic.h"
#include "arch/interrupts/irq.h"
#include "arch/io.h"
#include "dev/pci.h"
#include "kernel.h"
#include "log/log.h"
#include "mm/kheap.h"
#include "mm/page.h"
#include "mm/paging.h"
#include "uacpi/log.h"
#include "uacpi/platform/arch_helpers.h"
#include "uacpi/status.h"
#include "uacpi/types.h"
#include <util/memory.h>
#include <uacpi/kernel_api.h>
#include "util/semaphore.h"
#include "util/spinlock.h"
#include <log/nanoprintf.h>

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr *rsdp_addr) {
    uint64_t addr = kernel_info.rsdp_addr;

    if (!addr) {
        critical("acpi: RSDP address is 0!\n");
        return UACPI_STATUS_NOT_FOUND;
    }
    
    addr = VIRT_TO_PHYS(addr); // limine returns the RSDP address as a virtual address, but uacpi needs a physiucal one

    *rsdp_addr = addr;
    return UACPI_STATUS_OK;
}

void* uacpi_kernel_map(uacpi_phys_addr addr, uacpi_size len) {
    uint64_t aligned = ALIGN_DOWN(addr, PAGE_SIZE);
    size_t offset = (size_t)(addr - aligned);
    size_t actual_len = len + offset;
    size_t npages = ALIGN_UP(actual_len, PAGE_SIZE) / PAGE_SIZE;

    uint64_t vaddr = (uint64_t)PHYS_TO_VIRT(aligned);

    if (kernel_info.kernel_pt) {
        map_mmio(kernel_info.kernel_pt, vaddr, aligned, npages);
        reg_kernel_mmio(vaddr, aligned, npages);
    }

    return (void *)(vaddr + offset);
}

void uacpi_kernel_unmap(void *addr, uacpi_size len) {
    (void)addr;
    (void)len;
}

#ifndef UACPI_FORMATTED_LOGGING
void uacpi_kernel_log(uacpi_log_level log_level, const uacpi_char *fmt) {
    switch (log_level) {
        case UACPI_LOG_DEBUG:
            debug("acpi: %s", fmt);
            break;
        case UACPI_LOG_TRACE:
            debug("acpi: %s", fmt);
            break;
        case UACPI_LOG_INFO:
            info("acpi: %s", fmt);
            break;
        case UACPI_LOG_WARN:
            warn("acpi: %s", fmt);
            break;
        case UACPI_LOG_ERROR:
            error("acpi: %s", fmt);
            break;
        default:
            break;
    }
}
#else
void uacpi_kernel_log(uacpi_log_level log_level, const uacpi_char *fmt, ...) {
    char buffer[1024];

    va_list args;
    va_start(args, fmt);
    int length = npf_vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (length < 0 || length >= (int)sizeof(buffer)) {
        return;
    }

    switch (log_level) {
        case UACPI_LOG_DEBUG:
            debug("acpi: %s", buffer);
            break;
        case UACPI_LOG_TRACE:
            debug("acpi: %s", buffer);
            break;
        case UACPI_LOG_INFO:
            info("acpi: %s", buffer);
            break;
        case UACPI_LOG_WARN:
            warn("acpi: %s", buffer);
            break;
        case UACPI_LOG_ERROR:
            error("acpi: %s", buffer);
            break;
        default:
            break;
    }
}

void uacpi_kernel_vlog(uacpi_log_level log_level, const uacpi_char *fmt, uacpi_va_list va_args) {
    char buffer[1024];

    int length = npf_vsnprintf(buffer, sizeof(buffer), fmt, va_args);

    if (length < 0 || length >= (int)sizeof(buffer)) {
        return;
    }

    uacpi_kernel_log(log_level, "%s", buffer);
}
#endif // UACPI_FORMATTED_LOGGING

uacpi_interrupt_state uacpi_kernel_disable_interrupts(void) {
    uacpi_interrupt_state state = (uacpi_interrupt_state)_get_rflags();
    _cli();
    return state;
}

void uacpi_kernel_restore_interrupts(uacpi_interrupt_state state) {
    _set_rflags((uint64_t)state);
}

#ifndef UACPI_BAREBONES_MODE

#ifdef UACPI_KERNEL_INITIALIZATION
// TODO: + NOTE: basically other functions say "we need uacpi with XX init level" and then you 
//               init shit for that level :thumbsup:
uacpi_status uacpi_kernel_initialize(uacpi_init_level current_init_lvl) {
    switch (current_init_lvl) {
    case UACPI_INIT_LEVEL_EARLY:
        break;

    default:
        return UACPI_STATUS_DENIED;
    }

    return UACPI_STATUS_OK;
}


void uacpi_kernel_deinitialize(void) {
    return;
}
#endif // UACPI_KERNEL_INITIALIZATION

// TODO: implement
uacpi_status uacpi_kernel_pci_device_open(uacpi_pci_address address, uacpi_handle *out_handle) {
    uacpi_pci_address *addr = kzalloc(sizeof(uacpi_pci_address));
    if (!addr) {
        return UACPI_STATUS_OUT_OF_MEMORY;
    }

    *addr = address;
    *out_handle = addr;

    return UACPI_STATUS_OK;
}

// TODO: implement
void uacpi_kernel_pci_device_close(uacpi_handle handle) {
    if (!handle) {
        critical("acpi: tired to close nonexistent pci device handle");
    }

    kfree(handle);
    handle = NULL;
}

// TODO: implement
uacpi_status uacpi_kernel_pci_read8(uacpi_handle device, uacpi_size offset, uacpi_u8 *value) {
    uacpi_pci_address *addr = device;
    uint32_t val = pci_read_config(addr->bus, addr->device, addr->function, (uint8_t)(offset & ~3));
    *value = (uint8_t)(val >> ((offset & 3) * 8));
    return UACPI_STATUS_OK;
}
// TODO: implement
uacpi_status uacpi_kernel_pci_read16(uacpi_handle device, uacpi_size offset, uacpi_u16 *value) {
    uacpi_pci_address *addr = device;
    uint32_t val = pci_read_config(addr->bus, addr->device, addr->function, (uint8_t)(offset & ~3));
    *value = (uint16_t)(val >> ((offset & 2) * 8));
    return UACPI_STATUS_OK;
}
// TODO: implement
uacpi_status uacpi_kernel_pci_read32(uacpi_handle device, uacpi_size offset, uacpi_u32 *value) {
    uacpi_pci_address *addr = device;
    *value = pci_read_config(addr->bus, addr->device, addr->function, (uint8_t)offset);
    return UACPI_STATUS_OK;
}

// TODO: implement
uacpi_status uacpi_kernel_pci_write8(uacpi_handle device, uacpi_size offset, uacpi_u8 value) {
    uacpi_pci_address *addr = device;
    uint32_t val = pci_read_config(addr->bus, addr->device, addr->function, (uint8_t)(offset & ~3));
    uint32_t shift = (offset & 3) * 8;
    val = (val & ~(0xFFU << shift)) | ((uint32_t)value << shift);
    pci_write_config(addr->bus, addr->device, addr->function, (uint8_t)(offset & ~3), val);
    return UACPI_STATUS_OK;
}

// TODO: implement
uacpi_status uacpi_kernel_pci_write16(uacpi_handle device, uacpi_size offset, uacpi_u16 value) {
    uacpi_pci_address *addr = device;
    uint32_t val = pci_read_config(addr->bus, addr->device, addr->function, (uint8_t)(offset & ~3));
    uint32_t shift = (offset & 2) * 8;
    val = (val & ~(0xFFFFU << shift)) | ((uint32_t)value << shift);
    pci_write_config(addr->bus, addr->device, addr->function, (uint8_t)(offset & ~3), val);
    return UACPI_STATUS_OK;
}

// TODO: implement
uacpi_status uacpi_kernel_pci_write32(uacpi_handle device, uacpi_size offset, uacpi_u32 value) {
    uacpi_pci_address *addr = device;
    pci_write_config(addr->bus, addr->device, addr->function, (uint8_t)offset, value);
    return UACPI_STATUS_OK;
}

typedef struct {
    uint16_t base;
    uint16_t max_offset;
} io_t;

uacpi_status uacpi_kernel_io_map(uacpi_io_addr base, uacpi_size len, uacpi_handle *out_handle) {

    io_t *io = kzalloc(sizeof(io_t));
    if (!io) {
        critical("acpi: failed to allocate memory for io mapping\n");
        return UACPI_STATUS_OUT_OF_MEMORY;
    }

    io->base = (uint16_t)base;
    io->max_offset = (uint16_t)len;

    ((io_t **)out_handle)[0] = io;

    return UACPI_STATUS_OK;
}

void uacpi_kernel_io_unmap(uacpi_handle handle) {

    io_t *to_free = ((io_t **)handle)[0];

    if (!to_free) {
        return;
    }

    kfree(to_free);

    ((io_t **)handle)[0] = NULL;
}

uacpi_status uacpi_kernel_io_read8(uacpi_handle range, uacpi_size offset, uacpi_u8 *out_value) {
    io_t *io = range;
    if (!range) {
        return UACPI_STATUS_NOT_FOUND;
    }

    uacpi_u16 port = (uacpi_u16)io->base + (uacpi_u16)offset;
    out_value[0] = _inb(port);

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read16(uacpi_handle range, uacpi_size offset, uacpi_u16 *out_value) {
    io_t *io = range;
    if (!range) {
        return UACPI_STATUS_NOT_FOUND;
    }

    uacpi_u16 port = (uacpi_u16)io->base + (uacpi_u16)offset;
    out_value[0] = _inw(port);

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read32(uacpi_handle range, uacpi_size offset, uacpi_u32 *out_value) {
    io_t *io = range;
    if (!io) {
        return UACPI_STATUS_NOT_FOUND;
    }

    uacpi_u16 port = (uacpi_u16)io->base + (uacpi_u16)offset;
    out_value[0] = _ind(port);

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write8(uacpi_handle range, uacpi_size offset, uacpi_u8 in_value) {
    io_t *io = range;

    if (!io) {
        return UACPI_STATUS_NOT_FOUND;
    }

    uacpi_u16 port = (uacpi_u16)io->base + (uacpi_u16)offset;
    _outb(port, in_value);

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write16(uacpi_handle range, uacpi_size offset, uacpi_u16 in_value) {
    io_t *io = range;
    if (!io) {
        return UACPI_STATUS_NOT_FOUND;
    }

    uacpi_u16 port = (uacpi_u16)io->base + (uacpi_u16)offset;
    _outw(port, in_value);

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write32(uacpi_handle range, uacpi_size offset, uacpi_u32 in_value) {
    io_t *io = range;
    if (!io) {
        return UACPI_STATUS_NOT_FOUND;
    }

    uacpi_u16 port = (uacpi_u16)io->base + (uacpi_u16)offset;
    _outd(port, in_value);

    return UACPI_STATUS_OK;
}

void *uacpi_kernel_alloc(uacpi_size size) {
    void *ptr = kzalloc(size);
    if (!ptr) {
        critical("uacpi_alloc: failed to allocate %zu bytes\n", (size_t)size);
        return NULL;
    }

    memset(ptr, 0, size);

    return ptr;
}

#ifdef UACPI_NATIVE_ALLOC_ZEROED
void *uacpi_kernel_alloc_zeroed(uacpi_size size) {
    void *ptr = kzalloc(size);
    if (!ptr) {
        critical("uacpi_alloc_zeroed: failed to allocate %zu bytes\n", (size_t)size);
        return NULL;
    }

    memset(ptr, 0, size);

    return ptr;
}
#endif

#ifndef UACPI_SIZED_FREES
void uacpi_kernel_free(void *mem) {
    if (!mem) {
        return;
    }

    kfree(mem);
}
#else
void uacpi_kernel_free(void *mem, uacpi_size size_hint) {
    (void)size_hint;

    if (!mem) {
        return;
    }

    kfree(mem);
}
#endif

uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot(void) {
    return tsc_get_nanoseconds();
}

void uacpi_kernel_stall(uacpi_u8 usec) {
    for (uacpi_u8 i = 0; i < usec; i++)
        ;
}

void uacpi_kernel_sleep(uacpi_u64 msec) {
    lapic_timer_sleep_kernel(msec);
}


uacpi_handle uacpi_kernel_create_mutex(void) {
    spinlock_t *m = kmalloc(sizeof(spinlock_t));
    if (!m) return NULL;

    *m = (spinlock_t)SPINLOCK_INIT("uacpi_mutex");
    return (uacpi_handle)m;
}

void uacpi_kernel_free_mutex(uacpi_handle mutex) {
    if (!mutex) {
        warn("acpi: tried to free NULL mutex\n");
        return;
    }

    kfree(mutex);
}


uacpi_handle uacpi_kernel_create_event(void) {
    semaphore_t *semaphore = kmalloc(sizeof(semaphore_t));
    semaphore_init(semaphore, 0);

    return (uacpi_handle)semaphore;
}

void uacpi_kernel_free_event(uacpi_handle semaphore) {
    if (!semaphore)
        return;

    kfree(semaphore);
}

uacpi_thread_id uacpi_kernel_get_thread_id(void) {
    return NULL;
}

uacpi_status uacpi_kernel_acquire_mutex(uacpi_handle spinlock, uacpi_u16 timeout) {
    (void)timeout;

    spinlock_t *lock = (spinlock_t *)spinlock;
    if (!lock) {
        return UACPI_STATUS_NOT_FOUND;
    }

    spinlock_acquire(lock);
    return UACPI_STATUS_OK;
}

void uacpi_kernel_release_mutex(uacpi_handle spinlock) {
    spinlock_release(spinlock);
}

uacpi_bool uacpi_kernel_wait_for_event(uacpi_handle semaphore, uacpi_u16 timeout) {
    semaphore_t *sem = (semaphore_t *)semaphore;

    switch (timeout) {
    case 0xFFFF:
        for (;;) {
            if (semaphore_try_wait(sem)) {
                return UACPI_TRUE;
            }
            _pause(); // TODO: replace this with an actual sleep function
        }
        break;

    default:
        while (--timeout > 0) {
            if (semaphore_try_wait(sem)) {
                return UACPI_TRUE;
            }
            _pause(); // TODO: replace this with an actual sleep function
        }
        break;
    }

    return UACPI_FALSE;
}

void uacpi_kernel_signal_event(uacpi_handle semaphore) {
    semaphore_t *sem = (semaphore_t *)semaphore;
    if (!sem) {
        return;
    }

    semaphore_signal(sem);
}

void uacpi_kernel_reset_event(uacpi_handle semaphore) {
    semaphore_t *sem = (semaphore_t *)semaphore;
    if (!sem) {
        return;
    }

    semaphore_init(sem, 0);
}

uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request *fw_req) {
    switch (fw_req->type) {
    case UACPI_FIRMWARE_REQUEST_TYPE_BREAKPOINT:
        warn("acpi: firmware issued breakpoint request with code 0x%X\n", fw_req->fatal.code);
        break;

    case UACPI_FIRMWARE_REQUEST_TYPE_FATAL:
        critical("acpi: firmware issued fatal request with code 0x%X\n", fw_req->fatal.code);
        return UACPI_STATUS_INTERNAL_ERROR;

    default:
        break;
    }

    return UACPI_STATUS_OK;
}

typedef struct {
    uacpi_interrupt_handler handler;
    uacpi_handle ctx;
    int irq_vector;
} uacpi_irq_entry_t;

static uacpi_irq_entry_t uacpi_irq_entries[256] = {0};

static void uacpi_irq_dispatch(uint32_t irq, void *data, context_t *ctx) {
    (void)irq;
    (void)ctx;
    uacpi_irq_entry_t *entry = (uacpi_irq_entry_t *)data;

    if (!entry || !entry->handler) {
        warn("acpi: irq dispatch with no handler\n");
        return;
    }

    entry->handler(entry->ctx);
}

uacpi_status uacpi_kernel_install_interrupt_handler(uacpi_u32 irq, uacpi_interrupt_handler handler, uacpi_handle ctx, uacpi_handle *out_irq_handle) {
    if (irq >= 256 || !out_irq_handle || !handler) {
        return UACPI_STATUS_INVALID_ARGUMENT;
    }

    if (uacpi_irq_entries[irq].handler) {
        return UACPI_STATUS_ALREADY_EXISTS;
    }

    uacpi_irq_entry_t *entry = &uacpi_irq_entries[irq];
    entry->handler = handler;
    entry->ctx = ctx;

    int vector = irq_request((uint32_t)irq, uacpi_irq_dispatch, entry, "uacpi-irq");
    if (vector < 0) {
        entry->handler = NULL;
        entry->ctx = NULL;
        critical("acpi: failed to request IRQ for ACPI interrupt handler: %d\n", vector);
        return UACPI_STATUS_OUT_OF_MEMORY;
    }

    entry->irq_vector = vector;
    *out_irq_handle = (uacpi_handle)(uintptr_t)irq;
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_uninstall_interrupt_handler(uacpi_interrupt_handler handler, uacpi_handle irq_handle) {
    (void)handler;

    uintptr_t irq = (uintptr_t)irq_handle;
    if (irq >= 256) {
        return UACPI_STATUS_INVALID_ARGUMENT;
    }

    uacpi_irq_entry_t *entry = &uacpi_irq_entries[irq];
    if (!entry->handler) {
        return UACPI_STATUS_NOT_FOUND;
    }

    irq_free((uint32_t)entry->irq_vector);

    entry->handler = NULL;
    entry->ctx = NULL;
    entry->irq_vector = 0;

    return UACPI_STATUS_OK;
}

uacpi_handle uacpi_kernel_create_spinlock(void) {
    spinlock_t *lock = kmalloc(sizeof(spinlock_t));
    if (!lock) {
        return NULL;
    }

    *lock = (spinlock_t)SPINLOCK_INIT("uacpi_spinlock");
    return (uacpi_handle)lock;
}

void uacpi_kernel_free_spinlock(uacpi_handle atomic) {
    if (!atomic) {
        warn("acpi: tried to free NULL spinlock\n");
        return;
    }

    kfree(atomic);
}

uacpi_cpu_flags uacpi_kernel_lock_spinlock(uacpi_handle lock) {
    uacpi_cpu_flags flags = _get_rflags();
    spinlock_t *spinlock = (spinlock_t *)lock;
    if (!spinlock) {
        warn("acpi: tried to lock NULL spinlock\n");
        return 0;
    }
    
    spinlock_acquire(spinlock);

    return flags;
}

void uacpi_kernel_unlock_spinlock(uacpi_handle lock, uacpi_cpu_flags flags) {
    spinlock_t *spinlock = (spinlock_t *)lock;
    if (!spinlock) {
        warn("acpi: tried to unlock NULL spinlock\n");
        return;
    }

    spinlock_release(spinlock);
    _set_rflags((uint64_t)flags);
}

uacpi_status uacpi_kernel_schedule_work(uacpi_work_type work_type,
                                        uacpi_work_handler work_handler,
                                        uacpi_handle ctx) {
    (void)work_type;
    (void)work_handler;
    (void)ctx;

    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_wait_for_work_completion(void) {
    return UACPI_STATUS_UNIMPLEMENTED;
}

#endif // UACPI_BAREBONES_MODE