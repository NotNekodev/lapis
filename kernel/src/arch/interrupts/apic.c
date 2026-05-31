#include "arch/interrupts/pit.h"
#include "arch/io.h"
#include "kernel.h"
#include "log/log.h"
#include "mm/paging.h"
#include <arch/interrupts/irq.h>
#include "mm/page.h"
#include "uacpi/acpi.h"
#include "uacpi/status.h"
#include "uacpi/tables.h"
#include <arch/interrupts/apic.h>
#include <arch/interrupts/isr.h>
#include <stdint.h>

#define LAPIC_SIZE  0x1000

#define IA32_APIC_BASE_MSR 0x1B
#define LAPIC_REG_SVR         0x0F0
#define LAPIC_REG_TPR         0x080
#define LAPIC_REG_DFR         0x0E0
#define LAPIC_REG_LDR         0x0D0
#define LAPIC_REG_LVT_TIMER   0x320
#define LAPIC_REG_LVT_THERMAL 0x330
#define LAPIC_REG_LVT_PMC     0x340
#define LAPIC_REG_LVT_LINT0   0x350
#define LAPIC_REG_LVT_LINT1   0x360
#define LAPIC_REG_LVT_ERROR   0x370
#define LAPIC_REG_TIMER_INITCNT 0x380
#define LAPIC_REG_TIMER_CURRCNT 0x390
#define LAPIC_REG_TIMER_DIVIDE  0x3E0

#define APIC_LVT_MASKED       (1U << 16)
#define APIC_LVT_PERIODIC     (1U << 17)
#define APIC_SVR_ENABLE       (1U << 8)

#define IOAPIC_REG_SEL        0x00
#define IOAPIC_REG_WIN        0x10
#define IOAPIC_REG_ID         0x00
#define IOAPIC_REG_VER        0x01
#define IOAPIC_REG_TABLE_BASE 0x10

#define IOAPIC_REDIR_VECTOR_MASK 0xFF
#define IOAPIC_REDIR_POLARITY    (1U << 13)
#define IOAPIC_REDIR_TRIGGER     (1U << 15)
#define IOAPIC_REDIR_MASK        (1U << 16)

#define APIC_MADT_POLARITY_MASK_HEX 0x3
#define APIC_MADT_POLARITY_ACTIVE_LOW_HEX 0x3
#define APIC_MADT_TRIGGERING_MASK_HEX 0xC
#define APIC_MADT_TRIGGERING_LEVEL_HEX 0xC

static uint64_t lapic_virt;

static volatile uint32_t lapic_start;
static volatile uint32_t lapic_end;
static volatile uint8_t lapic_done;
static volatile uint64_t lapic_calib_count;
static volatile uint64_t lapic_timer_ticks;
static uint64_t lapic_freq_hz = 0;

static inline void lapic_write(uint32_t reg, uint32_t val) {
    *(volatile uint32_t *)(lapic_virt + reg) = val;
}

static inline uint32_t lapic_read(uint32_t reg) {
    return *(volatile uint32_t *)(lapic_virt + reg);
}

static inline void ioapic_write(struct ioapic_entry *io, uint8_t reg, uint32_t val) {
    volatile uint32_t *base =
        (volatile uint32_t *)PHYS_TO_VIRT(io->phys_addr);

    base[0] = reg;
    base[4] = val;
}

static inline uint32_t ioapic_read(struct ioapic_entry *io, uint8_t reg) {
    volatile uint32_t *base =
        (volatile uint32_t *)PHYS_TO_VIRT(io->phys_addr);

    base[0] = reg;
    return base[4];
}

static void apic_lookup_iso(uint8_t source, uint32_t *gsi, uint16_t *flags) {
    *gsi = source;
    *flags = 0;

    for (size_t i = 0; i < kernel_info.ioapic.iso_count; i++) {
        if (kernel_info.ioapic.iso_table[i].source == source) {
            *gsi = kernel_info.ioapic.iso_table[i].gsi;
            *flags = kernel_info.ioapic.iso_table[i].flags;
            return;
        }
    }
}

static struct ioapic_entry *apic_ioapic_for_gsi(uint32_t gsi, uint32_t *pin_out) {
    for (size_t i = 0; i < kernel_info.ioapic.ioapic_count; i++) {
        struct ioapic_entry *io = &kernel_info.ioapic.ioapics[i];
        uint32_t ver = ioapic_read(io, IOAPIC_REG_VER);
        uint32_t max_redir = (ver >> 16) & 0xFF;
        uint32_t base = io->gsi_base;

        if (gsi >= base && gsi <= base + max_redir) {
            if (pin_out) {
                *pin_out = gsi - base;
            }
            return io;
        }
    }

    return NULL;
}

static void apic_route_irq(uint8_t source, uint8_t vector) {
    uint32_t gsi;
    uint16_t flags;
    apic_lookup_iso(source, &gsi, &flags);

    uint32_t pin = 0;
    struct ioapic_entry *io = apic_ioapic_for_gsi(gsi, &pin);
    if (!io) {
        warn("apic: no IOAPIC found for source %u gsi %u\n", source, gsi);
        return;
    }

    uint32_t low = vector & IOAPIC_REDIR_VECTOR_MASK;

    uint16_t polarity = flags & APIC_MADT_POLARITY_MASK_HEX;
    uint16_t trigger = flags & APIC_MADT_TRIGGERING_MASK_HEX;

    if (polarity == APIC_MADT_POLARITY_ACTIVE_LOW_HEX) {
        low |= IOAPIC_REDIR_POLARITY;
    }

    if (trigger == APIC_MADT_TRIGGERING_LEVEL_HEX) {
        low |= IOAPIC_REDIR_TRIGGER;
    }

    uint32_t high = (apic_get_lapic_id() & 0xFF) << 24;
    uint8_t reg = IOAPIC_REG_TABLE_BASE + (uint8_t)(pin * 2);

    ioapic_write(io, reg + 1, high);
    ioapic_write(io, reg, low);

    info("apic: routed IRQ source=%u gsi=%u pin=%u vector=0x%02x\n",
         source, gsi, pin, vector);
}

static void apic_mask_irq(uint8_t source) {
    uint32_t gsi;
    uint16_t flags;
    apic_lookup_iso(source, &gsi, &flags);

    uint32_t pin = 0;
    struct ioapic_entry *io = apic_ioapic_for_gsi(gsi, &pin);
    if (!io) {
        warn("apic: no IOAPIC found to mask source %u gsi %u\n", source, gsi);
        return;
    }

    uint8_t reg = IOAPIC_REG_TABLE_BASE + (uint8_t)(pin * 2);
    uint32_t low = ioapic_read(io, reg);
    low |= IOAPIC_REDIR_MASK;
    ioapic_write(io, reg, low);

    info("apic: masked IRQ source=%u gsi=%u pin=%u\n", source, gsi, pin);
}

static void apic_spurious_isr(isr_t *self, context_t *ctx) {
    (void)self;
    (void)ctx;
}

static void apic_map_mmio(uint64_t phys, uint64_t size) {
    if (!phys || !size) {
        return;
    }

    uint64_t vaddr = (uint64_t)PHYS_TO_VIRT(phys);
    uint64_t npages = ALIGN_UP(size, PAGE_SIZE) / PAGE_SIZE;

    info("apic: mapping mmio phys=0x%.16llx vaddr=0x%.16llx pages=%llu\n",
         phys, vaddr, npages);

    if (kernel_info.kernel_pt) {
        map_mmio(kernel_info.kernel_pt, vaddr, phys, npages);
        reg_kernel_mmio(vaddr, phys, npages);
    }
}

static void ioapic_mask_all(void) {
    for (size_t i = 0; i < kernel_info.ioapic.ioapic_count; i++) {
        struct ioapic_entry *io = &kernel_info.ioapic.ioapics[i];
        uint32_t ver = ioapic_read(io, IOAPIC_REG_VER);
        uint32_t max_redir = (ver >> 16) & 0xFF;

        info("apic: ioapic id=%u gsi_base=%u ver=0x%08x max_redir=%u\n",
             io->id, io->gsi_base, ver, max_redir);

        for (uint32_t pin = 0; pin <= max_redir; pin++) {
            uint8_t reg = IOAPIC_REG_TABLE_BASE + (uint8_t)(pin * 2);
            uint32_t low = APIC_LVT_MASKED | 0x20;
            ioapic_write(io, reg, low);
            ioapic_write(io, reg + 1, 0);
        }
    }
}

static void lapic_enable(uint64_t lapic_phys) {
    uint64_t apic_base = _rdmsr(IA32_APIC_BASE_MSR);
    uint64_t msr_base = apic_base & 0xFFFFF000;

    info("apic: IA32_APIC_BASE before=0x%.16llx\n", apic_base);

    if (msr_base != lapic_phys) {
        info("apic: updating LAPIC base from 0x%.16llx to 0x%.16llx\n", msr_base, lapic_phys);
        apic_base &= ~0xFFFFF000ULL;
        apic_base |= lapic_phys & 0xFFFFF000ULL;
    }

    apic_base |= IA32_APIC_BASE_ENABLE;
    _wrmsr(IA32_APIC_BASE_MSR, apic_base);

    info("apic: IA32_APIC_BASE after=0x%.16llx\n", _rdmsr(IA32_APIC_BASE_MSR));

    lapic_virt = (uintptr_t)PHYS_TO_VIRT(lapic_phys);

    lapic_write(LAPIC_REG_TPR, 0);
    lapic_write(LAPIC_REG_DFR, 0xFFFFFFFF);
    lapic_write(LAPIC_REG_LDR, 0x01000000);

    lapic_write(LAPIC_REG_LVT_TIMER, APIC_LVT_MASKED);
    lapic_write(LAPIC_REG_LVT_THERMAL, APIC_LVT_MASKED);
    lapic_write(LAPIC_REG_LVT_PMC, APIC_LVT_MASKED);
    lapic_write(LAPIC_REG_LVT_LINT0, APIC_LVT_MASKED);
    lapic_write(LAPIC_REG_LVT_LINT1, APIC_LVT_MASKED);
    lapic_write(LAPIC_REG_LVT_ERROR, APIC_LVT_MASKED);

    register_interrupt(0xFF, apic_spurious_isr, NULL);
    lapic_write(LAPIC_REG_SVR, 0xFF | APIC_SVR_ENABLE);

    info("apic: LAPIC enabled, spurious vector=0xFF, id=0x%08x\n", lapic_read(LAPIC_REG_ID));
}

int apic_init(void) {
    struct acpi_madt *madt;

    uacpi_status ret = uacpi_table_find_by_signature(ACPI_MADT_SIGNATURE, (uacpi_table *)(&madt));
    if (uacpi_unlikely_error(ret)) {
        critical("apic: failed to get MADT table");
        return -1;
    }

    uint8_t *ptr = (uint8_t *)madt + sizeof(struct acpi_madt);
    uint8_t *end = (uint8_t *)madt + madt->hdr.length;

    uint64_t lapic_phys = madt->local_interrupt_controller_address;

    kernel_info.ioapic.ioapic_count = 0;
    kernel_info.ioapic.iso_count = 0;

    while (ptr < end) {
        struct acpi_entry_hdr *entry = (struct acpi_entry_hdr *)ptr;

        switch (entry->type) {
            case ACPI_MADT_ENTRY_TYPE_LAPIC: {
                struct acpi_madt_lapic *lapic_entry = (struct acpi_madt_lapic *)entry;
                if (lapic_entry->flags & ACPI_PIC_ENABLED) {
                    info("apic: found LAPIC with ACPI ID %d, APIC ID %d\n", lapic_entry->uid, lapic_entry->id);
                } else {
                    info("apic: found disabled LAPIC with ACPI ID %d, APIC ID %d\n", lapic_entry->uid, lapic_entry->id);
                }
                break;
            }
            case ACPI_MADT_ENTRY_TYPE_IOAPIC: {
                struct acpi_madt_ioapic *ioapic_entry = (struct acpi_madt_ioapic *)entry;
                info("apic: found IOAPIC with ID %d at GSI base 0x%x\n", ioapic_entry->id, ioapic_entry->gsi_base);

                if (kernel_info.ioapic.ioapic_count < MAX_IOAPICS) {
                    kernel_info.ioapic.ioapics[kernel_info.ioapic.ioapic_count].id = ioapic_entry->id;
                    kernel_info.ioapic.ioapics[kernel_info.ioapic.ioapic_count].gsi_base = ioapic_entry->gsi_base;
                    kernel_info.ioapic.ioapics[kernel_info.ioapic.ioapic_count].phys_addr = ioapic_entry->address;
                    kernel_info.ioapic.ioapic_count++;
                } else {
                    warn("apic: too many IOAPICs, ignoring IOAPIC with ID %d\n", ioapic_entry->id);
                }

                break;
            }
            case ACPI_MADT_ENTRY_TYPE_INTERRUPT_SOURCE_OVERRIDE: {
                struct acpi_madt_interrupt_source_override *iso_entry = (struct acpi_madt_interrupt_source_override *)entry;
                info("apic: found ISO for bus %d, source %d, GSI %d, flags=0x%04x\n",
                     iso_entry->bus, iso_entry->source, iso_entry->gsi, iso_entry->flags);

                if (kernel_info.ioapic.iso_count < MAX_ISO) {
                    kernel_info.ioapic.iso_table[kernel_info.ioapic.iso_count].source = iso_entry->source;
                    kernel_info.ioapic.iso_table[kernel_info.ioapic.iso_count].gsi = iso_entry->gsi;
                    kernel_info.ioapic.iso_table[kernel_info.ioapic.iso_count].flags = iso_entry->flags;
                    kernel_info.ioapic.iso_count++;
                } else {
                    warn("apic: too many ISOs, ignoring ISO for bus %d, source %d\n", iso_entry->bus, iso_entry->source);
                }
                break;
            }
            case ACPI_MADT_ENTRY_TYPE_NMI_SOURCE: {
                struct acpi_madt_nmi_source *nmi_source = (struct acpi_madt_nmi_source *)entry;
                info("apic: found NMI source at GSI %d, flags=0x%04x\n", nmi_source->gsi, nmi_source->flags);
                break;
            }
            case ACPI_MADT_ENTRY_TYPE_LAPIC_NMI: {
                struct acpi_madt_lapic_nmi *lapic_nmi = (struct acpi_madt_lapic_nmi *)entry;
                info("apic: found LAPIC NMI for UID %d on LINT%d, flags=0x%04x\n",
                     lapic_nmi->uid, lapic_nmi->lint, lapic_nmi->flags);
                break;
            }
            case ACPI_MADT_ENTRY_TYPE_LAPIC_ADDRESS_OVERRIDE: {
                struct acpi_madt_lapic_address_override *lapic_addr = (struct acpi_madt_lapic_address_override *)entry;
                info("apic: found LAPIC address override, new address is 0x%.16llx\n", lapic_addr->address);
                lapic_phys = lapic_addr->address;
                break;
            }
            default:
                warn("apic: unknown MADT entry type %d\n", entry->type);
        }

        ptr += entry->length;
    }

    if (!lapic_phys) {
        lapic_phys = _rdmsr(IA32_APIC_BASE_MSR) & 0xFFFFF000ULL;
    }

    info("apic: LAPIC physical address is 0x%.16llx\n", lapic_phys);

    apic_map_mmio(lapic_phys, LAPIC_SIZE);

    for (size_t i = 0; i < kernel_info.ioapic.ioapic_count; i++) {
        info("apic: registering IOAPIC id=%u gsi_base=%u phys=0x%.16llx\n",
             kernel_info.ioapic.ioapics[i].id,
             kernel_info.ioapic.ioapics[i].gsi_base,
             (uint64_t)kernel_info.ioapic.ioapics[i].phys_addr);
        apic_map_mmio(kernel_info.ioapic.ioapics[i].phys_addr, PAGE_SIZE);
    }

    if (kernel_info.ioapic.iso_count == 0) {
        info("apic: no interrupt source overrides present\n");
    }

    lapic_enable(lapic_phys);
    ioapic_mask_all();

    return 0;
}

bool is_apic_enabled(void) {
    return (_rdmsr(IA32_APIC_BASE_MSR) & IA32_APIC_BASE_ENABLE) != 0;
}

uint32_t apic_get_lapic_id(void) {
    if (lapic_virt == 0) {
        return 0;
    }

    return lapic_read(LAPIC_REG_ID) >> 24;
}

void apic_eoi(isr_t *self) {
    (void)self;
    if (lapic_virt != 0) {
        lapic_write(LAPIC_REG_EOI, 0);
        (void)lapic_read(LAPIC_REG_ID);
    }
}

static void pit_calibrate_irq(isr_t *self, context_t *ctx) {
    (void)self;
    (void)ctx;

    lapic_calib_count++;

    if (lapic_calib_count == 1)
        lapic_start = lapic_read(LAPIC_REG_TIMER_CURRCNT);

    if (lapic_calib_count == 100) {
        lapic_end = lapic_read(LAPIC_REG_TIMER_CURRCNT);
        lapic_done = 1;
    }

    apic_eoi(self);
}

void lapic_timer_init(uint8_t vector) {
    info("lapic: starting PIT calibration\n");

    lapic_done = 0;
    lapic_calib_count = 0;

    register_interrupt(0xFE, pit_calibrate_irq, NULL);
    apic_route_irq(0, 0xFE);

    pit_init(100);

    lapic_write(LAPIC_REG_TIMER_DIVIDE, 0x3);
    lapic_write(LAPIC_REG_LVT_TIMER, vector);
    lapic_write(LAPIC_REG_TIMER_INITCNT, 0xFFFFFFFF);

    _sti();

    uint64_t spins = 0;
    while (!lapic_done) {
        _pause();
        if (++spins > 200000000ULL) {
            warn("lapic: calibration timeout, no PIT IRQs\n");
            break;
        }
    }

    pit_stop();

    uint32_t lapic_delta = lapic_start - lapic_end;

    if (!lapic_delta) {
        warn("lapic: calibration failed\n");
        return;
    }

    uint32_t ticks_per_ms = lapic_delta / 100;

    info("lapic: ticks per ms = %u\n", ticks_per_ms);

    lapic_freq_hz = ticks_per_ms * 1000;

    uint32_t reload = ticks_per_ms;

    lapic_write(LAPIC_REG_TIMER_DIVIDE, 0x3);
    lapic_write(LAPIC_REG_LVT_TIMER, vector | APIC_LVT_PERIODIC);
    lapic_write(LAPIC_REG_TIMER_INITCNT, reload);

    info("lapic: timer initialized vector=0x%x frequency=%llu Hz\n", vector, lapic_freq_hz);
    unregister_interrupt(0xFE);
    apic_mask_irq(0);
}

static void apic_timer_irq_handler(uint32_t irq, void *data, context_t *ctx) {
    (void)irq;
    (void)data;
    (void)ctx;
    lapic_timer_ticks++;
    debug("apic: timer tick %llu\n", lapic_timer_ticks);
}

int apic_timer_test(void) {
    if (!is_apic_enabled() || lapic_virt == 0) {
        warn("apic: timer test skipped (apic enabled=%d lapic mapped=%d)\n",
             is_apic_enabled() ? 1 : 0, lapic_virt != 0 ? 1 : 0);
        return -1;
    }

    uint64_t rflags = _get_rflags();
    if (!(rflags & (1ULL << 9))) {
        warn("apic: timer test running with interrupts disabled\n");
    }

    lapic_timer_ticks = 0;

    int irq = irq_request_local(apic_timer_irq_handler, NULL, "lapic-timer-test");
    if (irq < 0) {
        warn("apic: timer test failed to allocate vector\n");
        return -1;
    }

    uint8_t vector = (uint8_t)irq;

    lapic_write(LAPIC_REG_TIMER_DIVIDE, 0x3);
    lapic_write(LAPIC_REG_LVT_TIMER, vector | APIC_LVT_PERIODIC);
    lapic_write(LAPIC_REG_TIMER_INITCNT, 0x100000);

    info("apic: timer test armed (periodic, vector=0x%02x)\n", vector);

    uint32_t start = lapic_read(LAPIC_REG_TIMER_CURRCNT);
    uint64_t spins = 0;
    while (lapic_timer_ticks == 0 && spins < 20000000) {
        spins++;
        _pause();
    }

    uint32_t end = lapic_read(LAPIC_REG_TIMER_CURRCNT);

    if (lapic_timer_ticks == 0) {
        warn("apic: timer test timeout start=0x%08x end=0x%08x\n", start, end);
        irq_free((uint32_t)irq);
        return -1;
    }

    info("apic: timer test ok ticks=%llu start=0x%08x end=0x%08x\n",
         lapic_timer_ticks, start, end);

    lapic_write(LAPIC_REG_LVT_TIMER, APIC_LVT_MASKED);
    irq_free((uint32_t)irq);
    return 0;
}