#ifndef _DEVICE_H
#define _DEVICE_H 1

#include <stdint.h>

struct bus;
struct driver;

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
} device_t;

device_t *device_create(const char *name);
void device_add_child(device_t *parent, device_t *child);

#endif // _DEVICE_H