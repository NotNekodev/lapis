#ifndef _BUS_H
#define _BUS_H 1

struct device;
struct driver;

typedef struct bus {
    const char *name;

    // enumerate hardware and scan devices
    int (*enumerate)(struct bus *bus);

    // match a device to a driver
    int (*match)(struct device *dev, struct driver *drv);

    // optional init function
    int (*init)(struct bus *bus);

    void *data;

    struct bus *next;
} bus_t;

void bus_register(bus_t *bus);
bus_t *bus_find(const char *name);

void bus_device_add(struct bus *bus, struct device *dev);

#endif // _BUS_H