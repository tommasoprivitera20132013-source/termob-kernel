#include "../include/driver.h"

#include "../include/heap.h"
#include "../include/klog.h"
#include "../include/serial.h"

typedef struct driver_bus_node {
    termob_bus_t bus;
    struct driver_bus_node* next;
} driver_bus_node_t;

typedef struct driver_device_node {
    termob_device_t device;
    struct driver_device_node* next;
} driver_device_node_t;

typedef struct driver_driver_node {
    termob_driver_t driver;
    struct driver_driver_node* next;
} driver_driver_node_t;

static int driver_model_ready;
static driver_bus_node_t* driver_bus_list;
static driver_device_node_t* driver_device_list;
static driver_driver_node_t* driver_driver_list;
static size_t driver_bus_count_value;
static size_t driver_device_count_value;
static size_t driver_driver_count_value;
static size_t driver_bound_count_value;

static void driver_log_append_char(char* text, size_t* index, size_t capacity, char c) {
    if (*index + 1U >= capacity) {
        return;
    }

    text[*index] = c;
    (*index)++;
    text[*index] = '\0';
}

static void driver_log_append_text(char* text,
                                   size_t* index,
                                   size_t capacity,
                                   const char* value) {
    size_t cursor;

    if (value == 0) {
        return;
    }

    cursor = 0U;
    while (value[cursor] != '\0') {
        driver_log_append_char(text, index, capacity, value[cursor]);
        cursor++;
    }
}

static void driver_log_append_hex8(char* text,
                                   size_t* index,
                                   size_t capacity,
                                   uint8_t value) {
    static const char hex_digits[] = "0123456789ABCDEF";

    driver_log_append_char(text, index, capacity, hex_digits[(value >> 4) & 0x0FU]);
    driver_log_append_char(text, index, capacity, hex_digits[value & 0x0FU]);
}

static void driver_log_bound(const termob_driver_t* driver, const termob_device_t* device) {
    char line[128];
    size_t index;

    index = 0U;
    line[0] = '\0';

    driver_log_append_text(line, &index, sizeof(line), "[DRIVER] bound ");
    driver_log_append_text(line, &index, sizeof(line), driver != 0 ? driver->name : "unknown");
    driver_log_append_text(line, &index, sizeof(line), " -> ");

    if (device != 0 && device->name != 0) {
        driver_log_append_text(line, &index, sizeof(line), device->name);
    } else if (device != 0 && device->bus_type == TERMOB_BUS_PCI) {
        driver_log_append_text(line, &index, sizeof(line), "pci-");
        driver_log_append_hex8(line, &index, sizeof(line), device->bus);
        driver_log_append_char(line, &index, sizeof(line), ':');
        driver_log_append_hex8(line, &index, sizeof(line), device->slot);
        driver_log_append_char(line, &index, sizeof(line), '.');
        driver_log_append_hex8(line, &index, sizeof(line), device->function);
    } else {
        driver_log_append_text(line, &index, sizeof(line), "unnamed-device");
    }

    klog_writeline(line);
    serial_writeline(line);
}

static driver_bus_node_t* driver_bus_node_by_type(termob_bus_type_t bus_type) {
    driver_bus_node_t* node;

    node = driver_bus_list;
    while (node != 0) {
        if (node->bus.type == bus_type) {
            return node;
        }
        node = node->next;
    }

    return 0;
}

static int driver_device_matches(const termob_driver_t* driver, const termob_device_t* device) {
    if (driver == 0 || device == 0) {
        return 0;
    }

    if (driver->bus_type != device->bus_type) {
        return 0;
    }

    if (driver->match != 0 && driver->match(device) == 0) {
        return 0;
    }

    return 1;
}

static void driver_try_bind(driver_device_node_t* device_node, driver_driver_node_t* driver_node) {
    if (device_node == 0 || driver_node == 0) {
        return;
    }

    if (device_node->device.driver != 0) {
        return;
    }

    if (!driver_device_matches(&driver_node->driver, &device_node->device)) {
        return;
    }

    device_node->device.driver = &driver_node->driver;
    device_node->device.bound_driver_name = driver_node->driver.name;
    driver_bound_count_value++;
    driver_log_bound(&driver_node->driver, &device_node->device);

    if (driver_node->driver.probe != 0) {
        driver_node->driver.probe(&device_node->device);
    }
}

static int driver_register_builtin_buses(void) {
    static const termob_bus_t pci_bus = { "PCI", TERMOB_BUS_PCI, 0 };
    static const termob_bus_t virtio_bus = { "VIRTIO", TERMOB_BUS_VIRTIO, 0 };

    if (!driver_register_bus(&pci_bus)) {
        return 0;
    }

    if (!driver_register_bus(&virtio_bus)) {
        return 0;
    }

    return 1;
}

void driver_model_init(void) {
    driver_model_ready = 0;
    driver_bus_list = 0;
    driver_device_list = 0;
    driver_driver_list = 0;
    driver_bus_count_value = 0U;
    driver_device_count_value = 0U;
    driver_driver_count_value = 0U;
    driver_bound_count_value = 0U;

    if (!heap_is_initialized()) {
        return;
    }

    driver_model_ready = 1;
    if (!driver_register_builtin_buses()) {
        driver_model_ready = 0;
    }
}

int driver_model_is_initialized(void) {
    return driver_model_ready;
}

int driver_register_bus(const termob_bus_t* bus) {
    driver_bus_node_t* node;

    if (!driver_model_ready || bus == 0 || bus->name == 0) {
        return 0;
    }

    if (driver_bus_node_by_type(bus->type) != 0) {
        return 1;
    }

    node = (driver_bus_node_t*)kmalloc(sizeof(driver_bus_node_t));
    if (node == 0) {
        return 0;
    }

    node->bus = *bus;
    node->next = 0;

    if (driver_bus_list == 0) {
        driver_bus_list = node;
    } else {
        driver_bus_node_t* tail;

        tail = driver_bus_list;
        while (tail->next != 0) {
            tail = tail->next;
        }
        tail->next = node;
    }

    driver_bus_count_value++;
    return 1;
}

const termob_bus_t* driver_bus_by_type(termob_bus_type_t bus_type) {
    driver_bus_node_t* node;

    node = driver_bus_node_by_type(bus_type);
    if (node == 0) {
        return 0;
    }

    return &node->bus;
}

size_t driver_bus_count(void) {
    return driver_bus_count_value;
}

int driver_bus_at(size_t index, termob_bus_t* out_bus) {
    driver_bus_node_t* node;
    size_t cursor;

    if (!driver_model_ready || out_bus == 0) {
        return 0;
    }

    node = driver_bus_list;
    cursor = 0U;
    while (node != 0) {
        if (cursor == index) {
            *out_bus = node->bus;
            return 1;
        }

        node = node->next;
        cursor++;
    }

    return 0;
}

int driver_register_device(const termob_device_t* device) {
    driver_device_node_t* node;
    driver_driver_node_t* driver_node;

    if (!driver_model_ready || device == 0 || device->name == 0) {
        return 0;
    }

    node = (driver_device_node_t*)kmalloc(sizeof(driver_device_node_t));
    if (node == 0) {
        return 0;
    }

    node->device = *device;
    node->device.driver = 0;
    node->device.bound_driver_name = 0;
    if (node->device.bus_ref == 0) {
        node->device.bus_ref = (termob_bus_t*)driver_bus_by_type(node->device.bus_type);
    }
    node->next = 0;

    if (driver_device_list == 0) {
        driver_device_list = node;
    } else {
        driver_device_node_t* tail;

        tail = driver_device_list;
        while (tail->next != 0) {
            tail = tail->next;
        }
        tail->next = node;
    }

    driver_device_count_value++;

    driver_node = driver_driver_list;
    while (driver_node != 0) {
        driver_try_bind(node, driver_node);
        driver_node = driver_node->next;
    }

    return 1;
}

int driver_register_driver(const termob_driver_t* driver) {
    driver_driver_node_t* node;
    driver_device_node_t* device_node;

    if (!driver_model_ready || driver == 0 || driver->name == 0) {
        return 0;
    }

    node = (driver_driver_node_t*)kmalloc(sizeof(driver_driver_node_t));
    if (node == 0) {
        return 0;
    }

    node->driver = *driver;
    node->next = 0;

    if (driver_driver_list == 0) {
        driver_driver_list = node;
    } else {
        driver_driver_node_t* tail;

        tail = driver_driver_list;
        while (tail->next != 0) {
            tail = tail->next;
        }
        tail->next = node;
    }

    driver_driver_count_value++;

    device_node = driver_device_list;
    while (device_node != 0) {
        driver_try_bind(device_node, node);
        device_node = device_node->next;
    }

    return 1;
}

size_t driver_device_count(void) {
    return driver_device_count_value;
}

size_t driver_bound_count(void) {
    return driver_bound_count_value;
}

size_t driver_driver_count(void) {
    return driver_driver_count_value;
}

int driver_device_at(size_t index, termob_device_t* out_device) {
    driver_device_node_t* node;
    size_t cursor;

    if (!driver_model_ready || out_device == 0) {
        return 0;
    }

    node = driver_device_list;
    cursor = 0U;
    while (node != 0) {
        if (cursor == index) {
            *out_device = node->device;
            return 1;
        }

        node = node->next;
        cursor++;
    }

    return 0;
}

int driver_driver_at(size_t index, termob_driver_t* out_driver) {
    driver_driver_node_t* node;
    size_t cursor;

    if (!driver_model_ready || out_driver == 0) {
        return 0;
    }

    node = driver_driver_list;
    cursor = 0U;
    while (node != 0) {
        if (cursor == index) {
            *out_driver = node->driver;
            return 1;
        }

        node = node->next;
        cursor++;
    }

    return 0;
}

const char* driver_bus_type_name(termob_bus_type_t bus_type) {
    const termob_bus_t* bus;

    bus = driver_bus_by_type(bus_type);
    if (bus != 0) {
        return bus->name;
    }

    switch (bus_type) {
        case TERMOB_BUS_PCI:
            return "PCI";
        case TERMOB_BUS_VIRTIO:
            return "VIRTIO";
        case TERMOB_BUS_NONE:
        default:
            return "NONE";
    }
}
