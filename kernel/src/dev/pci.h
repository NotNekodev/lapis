#ifndef _PCI_H
#define _PCI_H 1

#include <stdint.h>
#include <stdbool.h>

#include <dev/bus.h>
#include <dev/device.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define PCI_INVALID_VENDOR 0xFFFF

#define PCI_HEADER_TYPE_DEVICE     0x00
#define PCI_HEADER_TYPE_PCI_BRIDGE 0x01
#define PCI_HEADER_TYPE_CARDBUS    0x02

#define PCI_BAR_IO   0x1
#define PCI_BAR_MMIO 0x0

typedef struct pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;

    uint16_t vendor_id;
    uint16_t device_id;

    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;

    uint8_t header_type;
    uint8_t irq_line;

    uint32_t bar[6];

    uintptr_t bar_addr[6];
    bool bar_is_io[6];

    struct device *dev;
} pci_device_t;

void pci_init(bus_t *bus);
void pci_scan_bus(bus_t *bus, uint8_t busn);

pci_device_t *pci_get(struct device *dev);

// uacpi stuff
uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_write_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);

#endif // _PCI_H