#ifndef TERMOB_DRIVER_H
#define TERMOB_DRIVER_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    TERMOB_BUS_NONE = 0,
    TERMOB_BUS_PCI = 1,
    TERMOB_BUS_VIRTIO = 2
} termob_bus_type_t;

struct termob_driver;

typedef struct termob_bus {
    const char* name;
    termob_bus_type_t type;
    void* data;
} termob_bus_t;

typedef struct termob_device {
    const char* name;
    termob_bus_type_t bus_type;
    termob_bus_t* bus_ref;
    struct termob_driver* driver;
    const char* bound_driver_name;
    void* driver_data;
    void* data;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
} termob_device_t;

typedef struct termob_driver {
    const char* name;
    termob_bus_type_t bus_type;
    int (*match)(const termob_device_t* device);
    void (*probe)(termob_device_t* device);
    void (*remove)(termob_device_t* device);
    void* data;
} termob_driver_t;

void driver_model_init(void);
int driver_model_is_initialized(void);

int driver_register_bus(const termob_bus_t* bus);
const termob_bus_t* driver_bus_by_type(termob_bus_type_t bus_type);
size_t driver_bus_count(void);
int driver_bus_at(size_t index, termob_bus_t* out_bus);

int driver_register_device(const termob_device_t* device);
int driver_register_driver(const termob_driver_t* driver);

size_t driver_device_count(void);
size_t driver_bound_count(void);
size_t driver_driver_count(void);

int driver_device_at(size_t index, termob_device_t* out_device);
int driver_driver_at(size_t index, termob_driver_t* out_driver);

const char* driver_bus_type_name(termob_bus_type_t bus_type);

#endif
