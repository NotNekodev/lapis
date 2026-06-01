#ifndef _DEVICE_H
#define _DEVICE_H 1

#include <stdint.h>
#include <stdbool.h>

struct bus;
struct driver;
struct irq_desc;

typedef struct device_irq {
    uint32_t irq;
    uint32_t hwirq;

    bool allocated;
    bool shared;

    struct device_irq *next;
} device_irq_t;

typedef struct device {
    const char *name;

    struct device *next; // flat global list

    // hierarchy
    struct device *parent;
    struct device *children;
    struct device *next_sibling;

    // ownership
    struct bus *bus;
    struct driver *driver;

    // bus specific data
    void *data;

    // identity
    uint16_t vendor_id;
    uint16_t device_id;

    uint8_t class;
    uint8_t subclass;
    uint8_t prog_if;

    // state
    void *state;

    // irqs
    device_irq_t *irqs;
} device_t;

device_t *device_create(const char *name);
void device_add_child(device_t *parent, device_t *child);
void device_add_irq(device_t *dev, uint32_t irq);

#endif // _DEVICE_H