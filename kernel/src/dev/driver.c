#include "dev/driver.h"
#include "dev/device.h"
#include "dev/device_id.h"
#include "log/log.h"
#include "stddef.h"

static driver_t *driver_list = NULL;

extern device_t *global_device_list;

void driver_register(driver_t *drv) {
    drv->next = driver_list;
    driver_list = drv;

    debug("driver: registered %s\n", drv->name);

    for (device_t *dev = global_device_list; dev; dev = dev->next) {
        driver_try_bind(dev);
    }
}

static int match_device(const device_t *dev, const device_id_t *id) {
    if (id->class && id->class != dev->class) {
        return 0;
    }

    if (id->subclass && id->subclass != dev->subclass) {
        return 0;
    }

    if (id->prog_if && id->prog_if != dev->prog_if) {
        return 0;
    }

    if (id->vendor_id && id->vendor_id != dev->vendor_id) {
        return 0;
    }

    if (id->device_id && id->device_id != dev->device_id) {
        return 0;
    }

    return 1;
}

void driver_try_bind(struct device *dev) {
    for (driver_t *drv = driver_list; drv; drv = drv->next) {
        for (size_t i = 0; i < drv->id_count; i++) {
            if (match_device(dev, &drv->id_table[i])) {
                dev->driver = drv;
                debug("driver: %s bound to %s\n", drv->name, dev->name);

                if (drv->init) {
                    drv->init(dev);
                }

                return;
            }
        }
    }
}