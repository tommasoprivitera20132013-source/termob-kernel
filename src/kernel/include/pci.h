#ifndef TERMOB_PCI_H
#define TERMOB_PCI_H

#include <stddef.h>
#include <stdint.h>

void pci_init(void);
int pci_is_initialized(void);
size_t pci_device_count(void);
uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
uint8_t pci_config_read8(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value);
void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint16_t value);
const char* pci_class_name(uint8_t class_code, uint8_t subclass);
void pci_dump_to_terminal(void);

#endif
