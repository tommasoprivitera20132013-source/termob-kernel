#include "../include/device.h"
#include "../include/heap.h"
#include "../include/pci.h"
#include "../include/terminal.h"

#define PCI_CONFIG_ADDRESS_PORT 0xCF8U
#define PCI_CONFIG_DATA_PORT 0xCFCU

static int pci_ready;
static size_t pci_device_count_value;

static inline void pci_outl(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t pci_inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void pci_config_write32_internal(uint8_t bus,
                                        uint8_t slot,
                                        uint8_t function,
                                        uint8_t offset,
                                        uint32_t value) {
    uint32_t address;

    address = 0x80000000UL |
              ((uint32_t)bus << 16) |
              ((uint32_t)slot << 11) |
              ((uint32_t)function << 8) |
              ((uint32_t)offset & 0xFCU);

    pci_outl(PCI_CONFIG_ADDRESS_PORT, address);
    pci_outl(PCI_CONFIG_DATA_PORT, value);
}

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t address;

    address = 0x80000000UL |
              ((uint32_t)bus << 16) |
              ((uint32_t)slot << 11) |
              ((uint32_t)function << 8) |
              ((uint32_t)offset & 0xFCU);

    pci_outl(PCI_CONFIG_ADDRESS_PORT, address);
    return pci_inl(PCI_CONFIG_DATA_PORT);
}

uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t value;
    uint32_t shift;

    value = pci_config_read32(bus, slot, function, offset);
    shift = ((uint32_t)offset & 0x02U) * 8U;
    return (uint16_t)((value >> shift) & 0xFFFFU);
}

uint8_t pci_config_read8(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t value;
    uint32_t shift;

    value = pci_config_read32(bus, slot, function, offset);
    shift = ((uint32_t)offset & 0x03U) * 8U;
    return (uint8_t)((value >> shift) & 0xFFU);
}

void pci_config_write32(uint8_t bus,
                        uint8_t slot,
                        uint8_t function,
                        uint8_t offset,
                        uint32_t value) {
    pci_config_write32_internal(bus, slot, function, offset, value);
}

void pci_config_write16(uint8_t bus,
                        uint8_t slot,
                        uint8_t function,
                        uint8_t offset,
                        uint16_t value) {
    uint32_t aligned_offset;
    uint32_t current_value;
    uint32_t shift;
    uint32_t mask;

    aligned_offset = (uint32_t)offset & 0xFCU;
    current_value = pci_config_read32(bus, slot, function, (uint8_t)aligned_offset);
    shift = ((uint32_t)offset & 0x02U) * 8U;
    mask = 0xFFFFUL << shift;
    current_value = (current_value & ~mask) | ((uint32_t)value << shift);
    pci_config_write32_internal(bus, slot, function, (uint8_t)aligned_offset, current_value);
}

static void pci_write_u32(uint32_t value) {
    char digits[10];
    int i;

    if (value == 0U) {
        terminal_putchar('0');
        return;
    }

    i = 0;
    while (value > 0U && i < 10) {
        digits[i++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    while (i > 0) {
        terminal_putchar(digits[--i]);
    }
}

static void pci_write_hex8(uint8_t value) {
    static const char hex_digits[] = "0123456789ABCDEF";

    terminal_putchar(hex_digits[(value >> 4) & 0x0FU]);
    terminal_putchar(hex_digits[value & 0x0FU]);
}

static void pci_write_hex16(uint16_t value) {
    pci_write_hex8((uint8_t)((value >> 8) & 0xFFU));
    pci_write_hex8((uint8_t)(value & 0xFFU));
}

static char* pci_build_device_name(uint8_t bus, uint8_t slot, uint8_t function) {
    static const char hex_digits[] = "0123456789ABCDEF";
    char* name;

    name = (char*)kmalloc(12U);
    if (name == 0) {
        return 0;
    }

    name[0] = 'p';
    name[1] = 'c';
    name[2] = 'i';
    name[3] = '-';
    name[4] = hex_digits[(bus >> 4) & 0x0FU];
    name[5] = hex_digits[bus & 0x0FU];
    name[6] = ':';
    name[7] = hex_digits[(slot >> 4) & 0x0FU];
    name[8] = hex_digits[slot & 0x0FU];
    name[9] = '.';
    name[10] = hex_digits[function & 0x07U];
    name[11] = '\0';
    return name;
}

static void pci_register_function(uint8_t bus, uint8_t slot, uint8_t function) {
    kernel_device_t device;
    uint16_t vendor_id;

    vendor_id = pci_config_read16(bus, slot, function, 0x00U);
    if (vendor_id == 0xFFFFU) {
        return;
    }

    device.name = pci_build_device_name(bus, slot, function);
    if (device.name == 0) {
        device.name = "pci-device";
    }
    device.bus_type = TERMOB_BUS_PCI;
    device.bus_ref = (termob_bus_t*)driver_bus_by_type(TERMOB_BUS_PCI);
    device.driver = 0;
    device.driver_data = 0;
    device.data = 0;
    device.vendor_id = vendor_id;
    device.device_id = pci_config_read16(bus, slot, function, 0x02U);
    device.revision = pci_config_read8(bus, slot, function, 0x08U);
    device.prog_if = pci_config_read8(bus, slot, function, 0x09U);
    device.subclass = pci_config_read8(bus, slot, function, 0x0AU);
    device.class_code = pci_config_read8(bus, slot, function, 0x0BU);
    device.bus = bus;
    device.slot = slot;
    device.function = function;
    device.bound_driver_name = 0;

    if (device_register(&device)) {
        pci_device_count_value++;
    }
}

static void pci_scan_slot(uint8_t bus, uint8_t slot) {
    uint16_t vendor_id;
    uint8_t header_type;
    uint8_t function;

    vendor_id = pci_config_read16(bus, slot, 0U, 0x00U);
    if (vendor_id == 0xFFFFU) {
        return;
    }

    pci_register_function(bus, slot, 0U);

    header_type = pci_config_read8(bus, slot, 0U, 0x0EU);
    if ((header_type & 0x80U) == 0U) {
        return;
    }

    for (function = 1U; function < 8U; function++) {
        pci_register_function(bus, slot, function);
    }
}

void pci_init(void) {
    uint32_t bus;
    uint32_t slot;

    pci_ready = 1;
    pci_device_count_value = 0U;

    for (bus = 0U; bus < 256U; bus++) {
        for (slot = 0U; slot < 32U; slot++) {
            pci_scan_slot((uint8_t)bus, (uint8_t)slot);
        }
    }
}

int pci_is_initialized(void) {
    return pci_ready;
}

size_t pci_device_count(void) {
    return pci_device_count_value;
}

const char* pci_class_name(uint8_t class_code, uint8_t subclass) {
    switch (class_code) {
        case 0x01U:
            switch (subclass) {
                case 0x01U:
                    return "IDE controller";
                case 0x06U:
                    return "SATA controller";
                case 0x08U:
                    return "NVM controller";
                default:
                    return "Mass storage controller";
            }
        case 0x02U:
            switch (subclass) {
                case 0x00U:
                    return "Ethernet controller";
                default:
                    return "Network controller";
            }
        case 0x03U:
            switch (subclass) {
                case 0x00U:
                    return "VGA compatible controller";
                default:
                    return "Display controller";
            }
        case 0x04U:
            return "Multimedia controller";
        case 0x06U:
            switch (subclass) {
                case 0x00U:
                    return "Host bridge";
                case 0x01U:
                    return "ISA bridge";
                case 0x04U:
                    return "PCI bridge";
                default:
                    return "Bridge device";
            }
        case 0x0CU:
            switch (subclass) {
                case 0x03U:
                    return "USB controller";
                default:
                    return "Serial bus controller";
            }
        default:
            return "Unclassified device";
    }
}

void pci_dump_to_terminal(void) {
    kernel_device_t device;
    size_t index;

    terminal_writeline("PCI devices:");
    terminal_write("  Count      : ");
    pci_write_u32((uint32_t)pci_device_count_value);
    terminal_writeline(" devices");

    if (!pci_ready) {
        terminal_writeline("  PCI enumeration offline");
        return;
    }

    if (pci_device_count_value == 0U) {
        terminal_writeline("  No PCI devices detected");
        return;
    }

    for (index = 0U; device_at(index, &device); index++) {
        if (device.bus_type != TERMOB_BUS_PCI) {
            continue;
        }

        terminal_write("  ");
        pci_write_hex8(device.bus);
        terminal_putchar(':');
        pci_write_hex8(device.slot);
        terminal_putchar('.');
        pci_write_hex8(device.function);
        terminal_write("  ");
        pci_write_hex16(device.vendor_id);
        terminal_putchar(':');
        pci_write_hex16(device.device_id);
        terminal_write("  ");
        terminal_write(pci_class_name(device.class_code, device.subclass));

        if (device.bound_driver_name != 0) {
            terminal_write("  -> ");
            terminal_write(device.bound_driver_name);
        }

        terminal_putchar('\n');
    }
}
