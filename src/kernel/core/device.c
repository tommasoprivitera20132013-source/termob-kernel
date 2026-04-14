#include "../include/device.h"
#include "../include/terminal.h"

static void device_write_u32(uint32_t value) {
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

static void device_write_hex8(uint8_t value) {
    static const char hex_digits[] = "0123456789ABCDEF";

    terminal_putchar(hex_digits[(value >> 4) & 0x0FU]);
    terminal_putchar(hex_digits[value & 0x0FU]);
}

static void device_write_hex16(uint16_t value) {
    device_write_hex8((uint8_t)((value >> 8) & 0xFFU));
    device_write_hex8((uint8_t)(value & 0xFFU));
}

void device_model_init(void) {
    driver_model_init();
}

int device_model_is_initialized(void) {
    return driver_model_is_initialized();
}

int device_register(const kernel_device_t* device) {
    return driver_register_device(device);
}

int device_register_driver(const kernel_driver_t* driver) {
    return driver_register_driver(driver);
}

size_t device_count(void) {
    return driver_device_count();
}

size_t device_bound_count(void) {
    return driver_bound_count();
}

size_t driver_count(void) {
    return driver_driver_count();
}

int device_at(size_t index, kernel_device_t* out_device) {
    return driver_device_at(index, out_device);
}

int driver_at(size_t index, kernel_driver_t* out_driver) {
    return driver_driver_at(index, out_driver);
}

const char* device_bus_type_name(termob_bus_type_t bus_type) {
    return driver_bus_type_name(bus_type);
}

void device_dump_to_terminal(void) {
    kernel_device_t device;
    kernel_driver_t driver;
    termob_bus_t bus;
    size_t index;

    terminal_writeline("Driver model:");
    terminal_write("  State      : ");
    terminal_writeline(driver_model_is_initialized() ? "Ready" : "Offline");
    terminal_write("  Buses      : ");
    device_write_u32((uint32_t)driver_bus_count());
    terminal_writeline(" registered");
    terminal_write("  Devices    : ");
    device_write_u32((uint32_t)device_count());
    terminal_writeline(" total");
    terminal_write("  Bound      : ");
    device_write_u32((uint32_t)device_bound_count());
    terminal_writeline(" devices");
    terminal_write("  Drivers    : ");
    device_write_u32((uint32_t)driver_count());
    terminal_writeline(" registered");

    if (driver_bus_count() != 0U) {
        terminal_writeline("Registered buses:");
        for (index = 0U; driver_bus_at(index, &bus); index++) {
            terminal_write("  ");
            terminal_write(bus.name);
            terminal_write(" [");
            terminal_write(driver_bus_type_name(bus.type));
            terminal_writeline("]");
        }
    }

    if (driver_count() == 0U) {
        terminal_writeline("Registered drivers:");
        terminal_writeline("  No drivers registered yet");
    } else {
        terminal_writeline("Registered drivers:");
        for (index = 0U; driver_at(index, &driver); index++) {
            terminal_write("  ");
            terminal_write(driver.name);
            terminal_write(" [");
            terminal_write(device_bus_type_name(driver.bus_type));
            terminal_writeline("]");
        }
    }

    if (device_bound_count() == 0U) {
        terminal_writeline("Bound devices:");
        terminal_writeline("  No devices currently bound");
        return;
    }

    terminal_writeline("Bound devices:");
    for (index = 0U; device_at(index, &device); index++) {
        if (device.driver == 0) {
            continue;
        }

        terminal_write("  ");
        terminal_write(device.driver->name);
        terminal_write(" <- ");
        terminal_write(device.name != 0 ? device.name : "unnamed-device");

        if (device.bus_type == TERMOB_BUS_PCI) {
            terminal_write(" ");
            device_write_hex8(device.bus);
            terminal_putchar(':');
            device_write_hex8(device.slot);
            terminal_putchar('.');
            device_write_hex8(device.function);
            terminal_write(" ");
            device_write_hex16(device.vendor_id);
            terminal_putchar(':');
            device_write_hex16(device.device_id);
        }

        terminal_putchar('\n');
    }
}
