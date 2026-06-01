#include "dev/device.h"
#include "log/log.h"
#include "mm/kheap.h"
#include <stddef.h>

device_t *global_device_list = NULL;

device_t *device_create(const char *name) {
    device_t *dev = kzalloc(sizeof(device_t));
    if (!dev) return NULL;

    dev->name = name;

    dev->bus = NULL;
    dev->driver = NULL;

    dev->data = NULL;
    dev->state = NULL;

    dev->irqs = NULL;

    dev->next = NULL;
    dev->parent = NULL;
    dev->children = NULL;
    dev->next_sibling = NULL;

    return dev;
}

void device_add_child(device_t *parent, device_t *child) {
    if (!parent || !child) {
        return;
    }

    child->parent = parent;

    child->next_sibling = parent->children;
    parent->children = child;
}

void device_add_irq(device_t *dev, uint32_t hwirq) {
    if (!dev) return;

    device_irq_t *i = kzalloc(sizeof(device_irq_t));
    if (!i) return;

    i->hwirq = hwirq;
    i->irq = 0;
    i->allocated = false;
    i->shared = false;

    i->next = dev->irqs;
    dev->irqs = i;

    debug("device: %s added IRQ hwirq=%u\n", dev->name, hwirq);
}