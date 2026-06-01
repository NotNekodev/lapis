#include "dev/device.h"
#include "mm/kheap.h"
#include "stddef.h"

device_t *global_device_list = NULL;

device_t *device_create(const char *name) {
    device_t *dev = kzalloc(sizeof(device_t));

    if (!dev) {
        return NULL;
    }

    dev->name = name;

    dev->next = NULL;
    dev->parent = NULL;
    dev->children = NULL;
    dev->next_sibling = NULL;

    dev->bus = NULL;
    dev->driver = NULL;

    dev->data = NULL;
    dev->state = NULL;

    dev->vendor_id = 0;
    dev->device_id = 0;
    dev->class = 0;
    dev->subclass = 0;
    dev->prog_if = 0;

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