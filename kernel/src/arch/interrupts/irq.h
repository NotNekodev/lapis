#ifndef _IRQ_H
#define _IRQ_H 1

#include <stdint.h>
#include <stdbool.h>
#include <arch/interrupts/isr.h>

#define IRQ_VECTOR_MIN 0x20
#define IRQ_VECTOR_MAX 0xEF

typedef void (*irq_handler_t)(uint32_t irq, void *data, context_t *ctx);

typedef struct irq_domain irq_domain_t;

typedef struct irq_desc {
    uint32_t irq;
    uint32_t hwirq;
    uint8_t vector;
    irq_handler_t handler;
    void *data;
    const char *name;
    irq_domain_t *domain;
    bool enabled;
} irq_desc_t;

typedef struct irq_domain_ops {
    int (*alloc)(irq_domain_t *domain, uint32_t hwirq, uint32_t *out_irq, uint8_t *out_vector);
    void (*free)(irq_domain_t *domain, uint32_t hwirq, uint32_t irq, uint8_t vector);
    void (*enable)(irq_domain_t *domain, uint32_t hwirq, uint8_t vector);
    void (*disable)(irq_domain_t *domain, uint32_t hwirq, uint8_t vector);
    void (*eoi)(irq_domain_t *domain, uint32_t hwirq, uint8_t vector);
} irq_domain_ops_t;

struct irq_domain {
    const char *name;
    const irq_domain_ops_t *ops;
    uint32_t max_hwirq;
    uint32_t *hwirq_to_irq;
};

void irq_init(void);

int irq_request(uint32_t hwirq, irq_handler_t handler, void *data, const char *name);
int irq_request_local(irq_handler_t handler, void *data, const char *name);
void irq_free(uint32_t irq);

void irq_enable(uint32_t irq);
void irq_disable(uint32_t irq);

irq_desc_t *irq_get_desc(uint32_t irq);

#endif // _IRQ_H
