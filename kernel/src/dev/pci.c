#include "arch/io.h"
#include "dev/bus.h"
#include "dev/device.h"
#include "mm/kheap.h"
#include "log/log.h"
#include <dev/pci.h>
#include <log/nanoprintf.h>

void pci_write_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t address = (1 << 31)
        | ((uint32_t)bus << 16)
        | ((uint32_t)slot << 11)
        | ((uint32_t)func << 8)
        | (offset & 0xFC);

    _outd(PCI_CONFIG_ADDRESS, address);
    _outd(PCI_CONFIG_DATA, val);
}

uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (1 << 31)
        | ((uint32_t)bus << 16)
        | ((uint32_t)slot << 11)
        | ((uint32_t)func << 8)
        | (offset & 0xFC);

    _outd(PCI_CONFIG_ADDRESS, address);
    return _ind(PCI_CONFIG_DATA);
}

static inline uint16_t pci_vendor(uint8_t b, uint8_t s, uint8_t f) {
    return (uint16_t)pci_read_config(b, s, f, 0x00);
}

static inline uint16_t pci_device(uint8_t b, uint8_t s, uint8_t f) {
    return (uint16_t)(pci_read_config(b, s, f, 0x00) >> 16);
}

static inline uint8_t pci_class(uint8_t b, uint8_t s, uint8_t f) {
    return (pci_read_config(b, s, f, 0x08) >> 24);
}

static inline uint8_t pci_subclass(uint8_t b, uint8_t s, uint8_t f) {
    return (pci_read_config(b, s, f, 0x08) >> 16);
}

static inline uint8_t pci_progif(uint8_t b, uint8_t s, uint8_t f) {
    return (pci_read_config(b, s, f, 0x08) >> 8);
}

static inline uint8_t pci_header_type(uint8_t b, uint8_t s, uint8_t f) {
    return (pci_read_config(b, s, f, 0x0C) >> 16) & 0xFF;
}

static void pci_read_bars(pci_device_t *pdev) {
    for (int i = 0; i < 6; i++) {
        uint32_t bar = pci_read_config(pdev->bus, pdev->slot, pdev->func, 0x10 + i * 4);

        pdev->bar[i] = bar;

        if (!bar) continue;

        if (bar & 0x1) {
            pdev->bar_is_io[i] = true;
            pdev->bar_addr[i] = bar & ~0x3;
        } else {
            pdev->bar_is_io[i] = false;
            pdev->bar_addr[i] = bar & ~0xF;
        }
    }
}

static void pci_create_device(bus_t *bus, uint8_t b, uint8_t s, uint8_t f) {
    uint16_t vendor = pci_vendor(b, s, f);
    if (vendor == PCI_INVALID_VENDOR) return;

    pci_device_t *pdev = kzalloc(sizeof(pci_device_t));

    pdev->bus = b;
    pdev->slot = s;
    pdev->func = f;

    pdev->vendor_id = vendor;
    pdev->device_id = pci_device(b, s, f);

    pdev->class_code = pci_class(b, s, f);
    pdev->subclass = pci_subclass(b, s, f);
    pdev->prog_if = pci_progif(b, s, f);

    pdev->header_type = pci_header_type(b, s, f);

    uint8_t irq_line = (uint8_t)(pci_read_config(b, s, f, 0x3C));
    pdev->irq_line = irq_line;

    pci_read_bars(pdev);

    char *name = kzalloc(64);
    npf_snprintf(name, 64, "pci:%02x:%02x.%d", b, s, f);

    device_t *dev = device_create(name);

    dev->vendor_id = pdev->vendor_id;
    dev->device_id = pdev->device_id;
    dev->class = pdev->class_code;
    dev->subclass = pdev->subclass;
    dev->prog_if = pdev->prog_if;

    if (irq_line != 0xFF && irq_line != 0x00)
        device_add_irq(dev, irq_line);

    dev->data = pdev;
    pdev->dev = dev;

    debug("pci: %04x:%04x class=%02x subclass=%02x func=%d\n",
        pdev->vendor_id,
        pdev->device_id,
        pdev->class_code,
        pdev->subclass,
        pdev->func
    );

    bus_device_add(bus, dev);
}

static void pci_scan_function(bus_t *bus, uint8_t b, uint8_t s, uint8_t f) {
    pci_create_device(bus, b, s, f);
}

static void pci_scan_slot(bus_t *bus, uint8_t b, uint8_t s) {
    uint16_t vendor = pci_vendor(b, s, 0);

    if (vendor == PCI_INVALID_VENDOR) {
        return;
    }

    uint8_t header = pci_header_type(b, s, 0);
    bool multifunction = header & 0x80;

    for (uint8_t f = 0; f < 8; f++) {
        if (f == 0 || multifunction) {
            if (pci_vendor(b, s, f) != PCI_INVALID_VENDOR) {
                pci_scan_function(bus, b, s, f);
            }
        }
    }
}

void pci_scan_bus(bus_t *bus, uint8_t busn) {
    for (uint8_t s = 0; s < 32; s++) {
        pci_scan_slot(bus, busn, s);
    }
}

static int pci_enumerate(bus_t *bus) {
    for (uint16_t b = 0; b < 256; b++) {
        pci_scan_bus(bus, (uint8_t)b);
    }

    debug("pci: enumeration complete\n");
    return 0;
}

pci_device_t* pci_get(struct device *dev) {
    if (!dev || !dev->data) {
        return NULL;
    }

    return (pci_device_t *)dev->data;
}

void pci_init(bus_t *bus) {
    bus->enumerate = pci_enumerate;
    bus->name = "pci";
    bus->data = NULL;

    bus->enumerate(bus);
}