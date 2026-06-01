#ifndef _DRIVER_H
#define _DRIVER_H 1

#include "dev/device_id.h"
#include "arch/interrupts/irq.h"
#include <stddef.h>

struct device;

typedef struct driver {
    const char *name;

    const device_id_t *id_table;
    size_t id_count;

    int (*init)(struct device *dev);
    void (*remove)(struct device *dev);

    irq_handler_t irq_handler;

    struct driver *next;
} driver_t;

void driver_register(driver_t *drv);
void driver_try_bind(struct device *dev);

#endif // _DRIVER_H