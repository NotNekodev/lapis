#include "arch/io.h"
#include <dev/pci.h>

void pci_write_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t address = (1 << 31)
                     | ((uint32_t)bus   << 16)
                     | ((uint32_t)slot  << 11)
                     | ((uint32_t)func  <<  8)
                     | (offset & 0xFC);
    _outd(PCI_CONFIG_ADDRESS, address);
    _outd(PCI_CONFIG_DATA, val);
}

uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (1 << 31)
                     | ((uint32_t)bus   << 16)
                     | ((uint32_t)slot  << 11)
                     | ((uint32_t)func  <<  8)
                     | (offset & 0xFC);
    _outd(PCI_CONFIG_ADDRESS, address);
    return _ind(PCI_CONFIG_DATA);
}