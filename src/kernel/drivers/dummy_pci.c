#include "../include/dummy_pci.h"

#include "../include/driver.h"
#include "../include/klog.h"
#include "../include/serial.h"

#define DUMMY_PCI_VENDOR_ID 0xCAFEU
#define DUMMY_PCI_DEVICE_ID 0x0001U

static int dummy_pci_match(const termob_device_t* device) {
    if (device == 0 || device->bus_type != TERMOB_BUS_PCI) {
        return 0;
    }

    return device->vendor_id == DUMMY_PCI_VENDOR_ID &&
           device->device_id == DUMMY_PCI_DEVICE_ID;
}

static void dummy_pci_probe(termob_device_t* device) {
    device->driver_data = (void*)1;
    klog_writeline("TERMOB: dummy pci example probed");
    serial_writeline("TERMOB: dummy pci example probed");
}

static void dummy_pci_remove(termob_device_t* device) {
    if (device != 0) {
        device->driver_data = 0;
    }
}

void dummy_pci_register_example_driver(void) {
    static const termob_driver_t dummy_pci_driver = {
        "dummy-pci",
        TERMOB_BUS_PCI,
        dummy_pci_match,
        dummy_pci_probe,
        dummy_pci_remove,
        0
    };

    driver_register_driver(&dummy_pci_driver);
}

void dummy_pci_register_example_device(void) {
    static const char device_name[] = "dummy-pci-device0";
    termob_device_t device;

    device.name = device_name;
    device.bus_type = TERMOB_BUS_PCI;
    device.bus_ref = (termob_bus_t*)driver_bus_by_type(TERMOB_BUS_PCI);
    device.driver = 0;
    device.bound_driver_name = 0;
    device.driver_data = 0;
    device.data = 0;
    device.vendor_id = DUMMY_PCI_VENDOR_ID;
    device.device_id = DUMMY_PCI_DEVICE_ID;
    device.class_code = 0xFFU;
    device.subclass = 0x00U;
    device.prog_if = 0x00U;
    device.revision = 0x01U;
    device.bus = 0U;
    device.slot = 0U;
    device.function = 0U;

    driver_register_device(&device);
}
