#ifndef TERMOB_DEVICE_H
#define TERMOB_DEVICE_H

#include "driver.h"

typedef termob_device_t kernel_device_t;
typedef termob_driver_t kernel_driver_t;

void device_model_init(void);
int device_model_is_initialized(void);
int device_register(const kernel_device_t* device);
int device_register_driver(const kernel_driver_t* driver);
size_t device_count(void);
size_t device_bound_count(void);
size_t driver_count(void);
int device_at(size_t index, kernel_device_t* out_device);
int driver_at(size_t index, kernel_driver_t* out_driver);
const char* device_bus_type_name(termob_bus_type_t bus_type);
void device_dump_to_terminal(void);

#endif
