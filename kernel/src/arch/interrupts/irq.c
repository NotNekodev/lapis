#include <arch/interrupts/irq.h>

#include <arch/interrupts/apic.h>
#include <arch/interrupts/isr.h>
#include <kernel.h>
#include <log/log.h>
#include <mm/kheap.h>
#include <mm/page.h>
#include <util/memory.h>
#include <util/spinlock.h>

#define IOAPIC_REDIR_VECTOR_MASK 0xFF
#define IOAPIC_REDIR_POLARITY    (1U << 13)
#define IOAPIC_REDIR_TRIGGER     (1U << 15)
#define IOAPIC_REDIR_MASK        (1U << 16)

#define APIC_MADT_POLARITY_MASK_HEX 0x3
#define APIC_MADT_POLARITY_ACTIVE_LOW_HEX 0x3
#define APIC_MADT_TRIGGERING_MASK_HEX 0xC
#define APIC_MADT_TRIGGERING_LEVEL_HEX 0xC

static spinlock_t irq_lock = SPINLOCK_INIT("irq_lock");

static irq_desc_t irq_descs[256];
static uint8_t vector_used[256];

static irq_domain_t ioapic_domain;
static irq_domain_ops_t ioapic_ops;
static irq_domain_t lapic_domain;
static irq_domain_ops_t lapic_ops;

static inline void ioapic_write(struct ioapic_entry *io, uint8_t reg, uint32_t val) {
    volatile uint32_t *base = (volatile uint32_t *)PHYS_TO_VIRT(io->phys_addr);
    base[0] = reg;
    base[4] = val;
}

static inline uint32_t ioapic_read(struct ioapic_entry *io, uint8_t reg) {
    volatile uint32_t *base = (volatile uint32_t *)PHYS_TO_VIRT(io->phys_addr);
    base[0] = reg;
    return base[4];
}

static void irq_dispatch(isr_t *self, context_t *ctx) {
    uint8_t vector = (uint8_t)(self->id & 0xFF);
    irq_desc_t *desc = &irq_descs[vector];

    if (!desc->handler) {
        warn("irq: unhandled vector 0x%02x\n", vector);
        if (desc->domain && desc->domain->ops && desc->domain->ops->eoi) {
            desc->domain->ops->eoi(desc->domain, desc->hwirq, desc->vector);
        } else {
            apic_eoi(self);
        }
        return;
    }

    desc->handler(desc->irq, desc->data, ctx);

    if (desc->domain && desc->domain->ops && desc->domain->ops->eoi) {
        desc->domain->ops->eoi(desc->domain, desc->hwirq, desc->vector);
    } else {
        apic_eoi(self);
    }
}

static int vector_alloc(uint8_t *out_vector) {
    for (uint16_t v = IRQ_VECTOR_MIN; v <= IRQ_VECTOR_MAX; v++) {
        if (!vector_used[v]) {
            vector_used[v] = 1;
            *out_vector = (uint8_t)v;
            return 0;
        }
    }

    return -1;
}

static void vector_free(uint8_t vector) {
    if (vector >= IRQ_VECTOR_MIN && vector <= IRQ_VECTOR_MAX) {
        vector_used[vector] = 0;
    }
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

static int ioapic_domain_alloc(irq_domain_t *domain, uint32_t hwirq, uint32_t *out_irq, uint8_t *out_vector) {
    (void)domain;
    (void)hwirq;
    uint8_t vector;

    if (vector_alloc(&vector) != 0) {
        return -1;
    }

    *out_irq = vector;
    *out_vector = vector;
    return 0;
}

static void ioapic_domain_free(irq_domain_t *domain, uint32_t hwirq, uint32_t irq, uint8_t vector) {
    (void)domain;
    (void)hwirq;
    (void)irq;
    vector_free(vector);
}

static void ioapic_domain_enable(irq_domain_t *domain, uint32_t hwirq, uint8_t vector) {
    (void)domain;

    uint32_t gsi;
    uint16_t flags;
    apic_lookup_iso((uint8_t)hwirq, &gsi, &flags);

    uint32_t pin = 0;
    struct ioapic_entry *io = apic_ioapic_for_gsi(gsi, &pin);
    if (!io) {
        warn("irq: no IOAPIC found for hwirq %u gsi %u\n", hwirq, gsi);
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
}

static void ioapic_domain_disable(irq_domain_t *domain, uint32_t hwirq, uint8_t vector) {
    (void)domain;
    (void)vector;

    uint32_t gsi;
    uint16_t flags;
    apic_lookup_iso((uint8_t)hwirq, &gsi, &flags);

    uint32_t pin = 0;
    struct ioapic_entry *io = apic_ioapic_for_gsi(gsi, &pin);
    if (!io) {
        warn("irq: no IOAPIC found to mask hwirq %u gsi %u\n", hwirq, gsi);
        return;
    }

    uint8_t reg = IOAPIC_REG_TABLE_BASE + (uint8_t)(pin * 2);
    uint32_t low = ioapic_read(io, reg);
    low |= IOAPIC_REDIR_MASK;
    ioapic_write(io, reg, low);
}

static void ioapic_domain_eoi(irq_domain_t *domain, uint32_t hwirq, uint8_t vector) {
    (void)domain;
    (void)hwirq;
    (void)vector;
    apic_eoi(NULL);
}

static int lapic_domain_alloc(irq_domain_t *domain, uint32_t hwirq, uint32_t *out_irq, uint8_t *out_vector) {
    (void)domain;
    (void)hwirq;
    uint8_t vector;

    if (vector_alloc(&vector) != 0) {
        return -1;
    }

    *out_irq = vector;
    *out_vector = vector;
    return 0;
}

static void lapic_domain_free(irq_domain_t *domain, uint32_t hwirq, uint32_t irq, uint8_t vector) {
    (void)domain;
    (void)hwirq;
    (void)irq;
    vector_free(vector);
}

static void lapic_domain_enable(irq_domain_t *domain, uint32_t hwirq, uint8_t vector) {
    (void)domain;
    (void)hwirq;
    (void)vector;
}

static void lapic_domain_disable(irq_domain_t *domain, uint32_t hwirq, uint8_t vector) {
    (void)domain;
    (void)hwirq;
    (void)vector;
}

static void lapic_domain_eoi(irq_domain_t *domain, uint32_t hwirq, uint8_t vector) {
    (void)domain;
    (void)hwirq;
    (void)vector;
    apic_eoi(NULL);
}

void irq_init(void) {
    spinlock_acquire(&irq_lock);

    memset(irq_descs, 0, sizeof(irq_descs));
    memset(vector_used, 0, sizeof(vector_used));

    for (uint16_t v = 0; v < IRQ_VECTOR_MIN; v++) {
        vector_used[v] = 1;
    }
    for (uint16_t v = IRQ_VECTOR_MAX + 1; v < 256; v++) {
        vector_used[v] = 1;
    }

    ioapic_domain.name = "ioapic";
    ioapic_ops = (irq_domain_ops_t){
        .alloc = ioapic_domain_alloc,
        .free = ioapic_domain_free,
        .enable = ioapic_domain_enable,
        .disable = ioapic_domain_disable,
        .eoi = ioapic_domain_eoi,
    };
    ioapic_domain.ops = &ioapic_ops;
    ioapic_domain.max_hwirq = 256;
    ioapic_domain.hwirq_to_irq = kzalloc(sizeof(uint32_t) * ioapic_domain.max_hwirq);
    for (uint32_t i = 0; i < ioapic_domain.max_hwirq; i++) {
        ioapic_domain.hwirq_to_irq[i] = 0;
    }

    lapic_ops = (irq_domain_ops_t){
        .alloc = lapic_domain_alloc,
        .free = lapic_domain_free,
        .enable = lapic_domain_enable,
        .disable = lapic_domain_disable,
        .eoi = lapic_domain_eoi,
    };
    lapic_domain.name = "lapic";
    lapic_domain.ops = &lapic_ops;
    lapic_domain.max_hwirq = 1;
    lapic_domain.hwirq_to_irq = NULL;

    spinlock_release(&irq_lock);

    info("irq: initialized, vector range 0x%02x-0x%02x\n", IRQ_VECTOR_MIN, IRQ_VECTOR_MAX);
}

int irq_request(uint32_t hwirq, irq_handler_t handler, void *data, const char *name) {
    if (!handler) {
        return -1;
    }

    spinlock_acquire(&irq_lock);

    uint32_t irq = 0;
    uint8_t vector = 0;

    if (ioapic_domain.ops->alloc(&ioapic_domain, hwirq, &irq, &vector) != 0) {
        spinlock_release(&irq_lock);
        return -1;
    }

    irq_desc_t *desc = &irq_descs[vector];
    desc->irq = irq;
    desc->hwirq = hwirq;
    desc->vector = vector;
    desc->handler = handler;
    desc->data = data;
    desc->name = name;
    desc->domain = &ioapic_domain;
    desc->enabled = true;

    if (hwirq < ioapic_domain.max_hwirq) {
        ioapic_domain.hwirq_to_irq[hwirq] = irq;
    }

    register_interrupt(vector, irq_dispatch, NULL);
    ioapic_domain.ops->enable(&ioapic_domain, hwirq, vector);

    spinlock_release(&irq_lock);

    info("irq: registered hwirq=%u vector=0x%02x name=%s\n", hwirq, vector, name ? name : "(null)");
    return (int)irq;
}

int irq_request_local(irq_handler_t handler, void *data, const char *name) {
    if (!handler) {
        return -1;
    }

    spinlock_acquire(&irq_lock);

    uint32_t irq = 0;
    uint8_t vector = 0;

    if (lapic_domain.ops->alloc(&lapic_domain, 0, &irq, &vector) != 0) {
        spinlock_release(&irq_lock);
        return -1;
    }

    irq_desc_t *desc = &irq_descs[vector];
    desc->irq = irq;
    desc->hwirq = 0;
    desc->vector = vector;
    desc->handler = handler;
    desc->data = data;
    desc->name = name;
    desc->domain = &lapic_domain;
    desc->enabled = true;

    register_interrupt(vector, irq_dispatch, NULL);

    spinlock_release(&irq_lock);

    info("irq: registered local vector=0x%02x name=%s\n", vector, name ? name : "(null)");
    return (int)irq;
}

void irq_free(uint32_t irq) {
    if (irq >= 256) {
        return;
    }

    spinlock_acquire(&irq_lock);

    irq_desc_t *desc = &irq_descs[irq];
    if (!desc->handler || !desc->domain) {
        spinlock_release(&irq_lock);
        return;
    }

    desc->domain->ops->disable(desc->domain, desc->hwirq, desc->vector);
    desc->domain->ops->free(desc->domain, desc->hwirq, desc->irq, desc->vector);

    if (desc->hwirq < desc->domain->max_hwirq) {
        desc->domain->hwirq_to_irq[desc->hwirq] = 0;
    }

    unregister_interrupt(desc->vector);
    memset(desc, 0, sizeof(*desc));

    spinlock_release(&irq_lock);
}

void irq_enable(uint32_t irq) {
    if (irq >= 256) {
        return;
    }

    irq_desc_t *desc = &irq_descs[irq];
    if (desc->domain && desc->domain->ops && desc->domain->ops->enable) {
        desc->domain->ops->enable(desc->domain, desc->hwirq, desc->vector);
        desc->enabled = true;
    }
}

void irq_disable(uint32_t irq) {
    if (irq >= 256) {
        return;
    }

    irq_desc_t *desc = &irq_descs[irq];
    if (desc->domain && desc->domain->ops && desc->domain->ops->disable) {
        desc->domain->ops->disable(desc->domain, desc->hwirq, desc->vector);
        desc->enabled = false;
    }
}

irq_desc_t *irq_get_desc(uint32_t irq) {
    if (irq >= 256) {
        return NULL;
    }

    return &irq_descs[irq];
}
