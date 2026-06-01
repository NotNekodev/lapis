
#include "dev/bus.h"
#include "dev/device.h"
#include "dev/driver.h"
#include "log/log.h"
#include "stddef.h"
#include <util/memory.h>

static bus_t *bus_list = NULL;

extern device_t *global_device_list;

void bus_register(bus_t *bus) {
    bus->next = bus_list;
    bus_list = bus;

    debug("bus: registered %s\n", bus->name);

    if (bus->init) {
        bus->init(bus);
    }

    if (bus->enumerate) {
        bus->enumerate(bus);
    }
}

bus_t *bus_find(const char *name) {
    for (bus_t *bus = bus_list; bus; bus = bus->next) {
        if (strcmp(bus->name, name) == 0) {
            return bus;
        }
    }
    return NULL;
}

void bus_device_add(struct bus *bus, struct device *dev) {
    (void)bus;

    dev->next = global_device_list;
    global_device_list = dev;

    debug("bus: device added %s\n", dev->name);

    driver_try_bind(dev);
}