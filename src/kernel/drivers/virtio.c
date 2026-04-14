#include "../include/block.h"
#include "../include/device.h"
#include "../include/driver.h"
#include "../include/heap.h"
#include "../include/klog.h"
#include "../include/pci.h"
#include "../include/serial.h"
#include "../include/terminal.h"
#include "../include/timer.h"
#include "../include/virtio.h"

#define VIRTIO_PCI_VENDOR_ID 0x1AF4U
#define VIRTIO_PCI_BLOCK_DEVICE_ID 0x1001U
#define VIRTIO_PCI_MODERN_BLOCK_DEVICE_ID 0x1042U

#define PCI_COMMAND_REGISTER 0x04U
#define PCI_COMMAND_IO_SPACE 0x0001U
#define PCI_COMMAND_BUS_MASTER 0x0004U
#define PCI_BAR0_REGISTER 0x10U

#define VIRTIO_PCI_HOST_FEATURES 0x00U
#define VIRTIO_PCI_GUEST_FEATURES 0x04U
#define VIRTIO_PCI_QUEUE_PFN 0x08U
#define VIRTIO_PCI_QUEUE_NUM 0x0CU
#define VIRTIO_PCI_QUEUE_SEL 0x0EU
#define VIRTIO_PCI_QUEUE_NOTIFY 0x10U
#define VIRTIO_PCI_STATUS 0x12U
#define VIRTIO_PCI_ISR 0x13U
#define VIRTIO_PCI_CONFIG 0x14U

#define VIRTIO_STATUS_ACKNOWLEDGE 0x01U
#define VIRTIO_STATUS_DRIVER 0x02U
#define VIRTIO_STATUS_DRIVER_OK 0x04U
#define VIRTIO_STATUS_FAILED 0x80U

#define VIRTQ_DESC_F_NEXT 1U
#define VIRTQ_DESC_F_WRITE 2U

#define VIRTIO_BLK_T_IN 0U
#define VIRTIO_BLK_S_OK 0U

#define TERMOB_VIRTIO_BLK_SECTOR_SIZE 512U

typedef struct {
    uint16_t transport_vendor_id;
    uint16_t transport_device_id;
    uint8_t transport_bus;
    uint8_t transport_slot;
    uint8_t transport_function;
} virtio_device_info_t;

typedef struct {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} __attribute__((packed)) virtio_blk_req_header_t;

typedef struct {
    const virtio_device_info_t* transport;
    uint32_t capacity_sectors;
    uint32_t block_size_bytes;
    uint16_t io_base;
    uint16_t queue_size;
    uint16_t last_used_index;
    void* queue_memory;
    uint32_t queue_memory_bytes;
    volatile void* desc_table;
    volatile void* avail_ring;
    volatile void* used_ring;
    virtio_blk_req_header_t* request_header;
    uint8_t* request_status;
    int block_registered;
    int legacy_transport;
    int ready;
} virtio_blk_state_t;

typedef struct {
    uint64_t address;
    uint32_t length;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed)) virtq_desc_t;

typedef struct {
    uint16_t flags;
    uint16_t index;
} __attribute__((packed)) virtq_avail_t;

typedef struct {
    uint32_t id;
    uint32_t length;
} __attribute__((packed)) virtq_used_elem_t;

typedef struct {
    uint16_t flags;
    uint16_t index;
} __attribute__((packed)) virtq_used_t;

static int virtio_is_modern_device_id(uint16_t device_id);

static int virtio_ready;
static size_t virtio_transport_bound_count_value;
static size_t virtio_logical_device_count_value;
static size_t virtio_blk_bound_count_value;

static void virtio_write_u32(uint32_t value) {
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

static void virtio_write_hex8(uint8_t value) {
    static const char hex_digits[] = "0123456789ABCDEF";

    terminal_putchar(hex_digits[(value >> 4) & 0x0FU]);
    terminal_putchar(hex_digits[value & 0x0FU]);
}

static void virtio_write_hex16(uint16_t value) {
    virtio_write_hex8((uint8_t)((value >> 8) & 0xFFU));
    virtio_write_hex8((uint8_t)(value & 0xFFU));
}

static inline uint8_t virtio_inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint16_t virtio_inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint32_t virtio_inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void virtio_outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void virtio_outw(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline void virtio_outl(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static uint32_t virtio_align_up_u32(uint32_t value, uint32_t alignment) {
    return (value + (alignment - 1U)) & ~(alignment - 1U);
}

static void virtio_log_event(const char* message) {
    klog_writeline(message);
    serial_writeline(message);
}

static uint16_t virtio_pci_bar0_io_base(const virtio_device_info_t* transport) {
    uint32_t bar_value;

    if (transport == 0) {
        return 0U;
    }

    bar_value = pci_config_read32(transport->transport_bus,
                                  transport->transport_slot,
                                  transport->transport_function,
                                  PCI_BAR0_REGISTER);
    if ((bar_value & 0x01U) == 0U) {
        return 0U;
    }

    return (uint16_t)(bar_value & 0xFFF0U);
}

static void virtio_pci_enable_io_busmaster(const virtio_device_info_t* transport) {
    uint16_t command;

    if (transport == 0) {
        return;
    }

    command = pci_config_read16(transport->transport_bus,
                                transport->transport_slot,
                                transport->transport_function,
                                PCI_COMMAND_REGISTER);
    command |= (uint16_t)(PCI_COMMAND_IO_SPACE | PCI_COMMAND_BUS_MASTER);
    pci_config_write16(transport->transport_bus,
                       transport->transport_slot,
                       transport->transport_function,
                       PCI_COMMAND_REGISTER,
                       command);
}

static uint32_t virtio_blk_queue_bytes(uint16_t queue_size) {
    uint32_t desc_bytes;
    uint32_t avail_bytes;
    uint32_t used_offset;
    uint32_t used_bytes;

    desc_bytes = (uint32_t)sizeof(virtq_desc_t) * queue_size;
    avail_bytes = 4U + ((uint32_t)queue_size * sizeof(uint16_t));
    used_offset = virtio_align_up_u32(desc_bytes + avail_bytes, 4096U);
    used_bytes = 4U + ((uint32_t)queue_size * sizeof(virtq_used_elem_t));
    return used_offset + used_bytes;
}

static uint16_t* virtio_avail_ring_entries(virtio_blk_state_t* state) {
    return (uint16_t*)((uintptr_t)state->avail_ring + sizeof(virtq_avail_t));
}

static int virtio_blk_setup_queue(virtio_blk_state_t* state) {
    uintptr_t aligned_base;
    uint32_t queue_bytes;
    uint32_t desc_bytes;
    uint32_t avail_bytes;
    uint32_t used_offset;
    uint16_t queue_size;
    uint8_t status;
    uint32_t capacity_low;
    uint32_t capacity_high;

    if (state == 0 || state->transport == 0) {
        return 0;
    }

    if (virtio_is_modern_device_id(state->transport->transport_device_id)) {
        virtio_log_event("TERMOB: virtio-blk modern pci transport not implemented yet");
        return 0;
    }

    virtio_pci_enable_io_busmaster(state->transport);
    state->io_base = virtio_pci_bar0_io_base(state->transport);
    if (state->io_base == 0U) {
        virtio_log_event("TERMOB: virtio-blk legacy io base unavailable");
        return 0;
    }

    virtio_outb((uint16_t)(state->io_base + VIRTIO_PCI_STATUS), 0U);
    virtio_outb((uint16_t)(state->io_base + VIRTIO_PCI_STATUS),
                (uint8_t)(VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER));
    virtio_outl((uint16_t)(state->io_base + VIRTIO_PCI_GUEST_FEATURES), 0U);

    virtio_outw((uint16_t)(state->io_base + VIRTIO_PCI_QUEUE_SEL), 0U);
    queue_size = virtio_inw((uint16_t)(state->io_base + VIRTIO_PCI_QUEUE_NUM));
    if (queue_size == 0U) {
        virtio_log_event("TERMOB: virtio-blk queue 0 unavailable");
        virtio_outb((uint16_t)(state->io_base + VIRTIO_PCI_STATUS), VIRTIO_STATUS_FAILED);
        return 0;
    }

    queue_bytes = virtio_blk_queue_bytes(queue_size);
    state->queue_memory = kmalloc(queue_bytes + 4096U);
    if (state->queue_memory == 0) {
        virtio_log_event("TERMOB: virtio-blk queue allocation failed");
        virtio_outb((uint16_t)(state->io_base + VIRTIO_PCI_STATUS), VIRTIO_STATUS_FAILED);
        return 0;
    }

    aligned_base = virtio_align_up_u32((uint32_t)(uintptr_t)state->queue_memory, 4096U);
    state->queue_memory_bytes = queue_bytes;
    state->queue_size = queue_size;
    state->last_used_index = 0U;

    desc_bytes = (uint32_t)sizeof(virtq_desc_t) * queue_size;
    avail_bytes = 4U + ((uint32_t)queue_size * sizeof(uint16_t));
    used_offset = virtio_align_up_u32(desc_bytes + avail_bytes, 4096U);

    state->desc_table = (volatile void*)aligned_base;
    state->avail_ring = (volatile void*)(aligned_base + desc_bytes);
    state->used_ring = (volatile void*)(aligned_base + used_offset);

    {
        uint8_t* mem;
        uint32_t index;

        mem = (uint8_t*)aligned_base;
        for (index = 0U; index < queue_bytes; index++) {
            mem[index] = 0U;
        }
    }

    virtio_outl((uint16_t)(state->io_base + VIRTIO_PCI_QUEUE_PFN),
                (uint32_t)(aligned_base >> 12));

    capacity_low = virtio_inl((uint16_t)(state->io_base + VIRTIO_PCI_CONFIG));
    capacity_high = virtio_inl((uint16_t)(state->io_base + VIRTIO_PCI_CONFIG + 4U));
    if (capacity_high == 0U) {
        state->capacity_sectors = capacity_low;
    } else {
        state->capacity_sectors = 0U;
    }

    state->block_size_bytes = TERMOB_VIRTIO_BLK_SECTOR_SIZE;
    state->legacy_transport = 1;

    status = (uint8_t)(VIRTIO_STATUS_ACKNOWLEDGE |
                       VIRTIO_STATUS_DRIVER |
                       VIRTIO_STATUS_DRIVER_OK);
    virtio_outb((uint16_t)(state->io_base + VIRTIO_PCI_STATUS), status);
    return 1;
}

static int virtio_blk_block_read(void* private_data,
                                 uint32_t lba,
                                 uint32_t sector_count,
                                 void* buffer) {
    uint16_t* avail_ring;
    virtq_avail_t* avail;
    volatile virtq_used_t* used;
    virtq_desc_t* desc;
    virtio_blk_state_t* state;
    uint32_t data_bytes;
    uint32_t start_ticks;

    state = (virtio_blk_state_t*)private_data;
    if (state == 0 || !state->ready || !state->legacy_transport || buffer == 0) {
        return 0;
    }

    if (sector_count == 0U || sector_count > 1U) {
        return 0;
    }

    if (state->capacity_sectors != 0U && lba >= state->capacity_sectors) {
        return 0;
    }

    desc = (virtq_desc_t*)(uintptr_t)state->desc_table;
    avail = (virtq_avail_t*)(uintptr_t)state->avail_ring;
    used = (volatile virtq_used_t*)(uintptr_t)state->used_ring;
    avail_ring = virtio_avail_ring_entries(state);

    if (state->request_header == 0 || state->request_status == 0) {
        return 0;
    }

    data_bytes = sector_count * state->block_size_bytes;
    state->request_header->type = VIRTIO_BLK_T_IN;
    state->request_header->reserved = 0U;
    state->request_header->sector = (uint64_t)lba;
    *state->request_status = 0xFFU;

    desc[0].address = (uint64_t)(uintptr_t)state->request_header;
    desc[0].length = sizeof(virtio_blk_req_header_t);
    desc[0].flags = VIRTQ_DESC_F_NEXT;
    desc[0].next = 1U;

    desc[1].address = (uint64_t)(uintptr_t)buffer;
    desc[1].length = data_bytes;
    desc[1].flags = (uint16_t)(VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE);
    desc[1].next = 2U;

    desc[2].address = (uint64_t)(uintptr_t)state->request_status;
    desc[2].length = 1U;
    desc[2].flags = VIRTQ_DESC_F_WRITE;
    desc[2].next = 0U;

    avail_ring[avail->index % state->queue_size] = 0U;
    __asm__ volatile ("" ::: "memory");
    avail->index++;
    __asm__ volatile ("" ::: "memory");
    virtio_outw((uint16_t)(state->io_base + VIRTIO_PCI_QUEUE_NOTIFY), 0U);

    start_ticks = timer_get_ticks();
    while ((uint16_t)(used->index - state->last_used_index) == 0U) {
        if ((timer_get_ticks() - start_ticks) > timer_get_frequency_hz()) {
            return 0;
        }
    }

    state->last_used_index = used->index;
    (void)virtio_inb((uint16_t)(state->io_base + VIRTIO_PCI_ISR));
    return *state->request_status == VIRTIO_BLK_S_OK;
}

static void virtio_copy_text(char* dst, size_t capacity, const char* src) {
    size_t index;

    if (dst == 0 || capacity == 0U) {
        return;
    }

    index = 0U;
    while (index + 1U < capacity && src != 0 && src[index] != '\0') {
        dst[index] = src[index];
        index++;
    }
    dst[index] = '\0';
}

static int virtio_is_supported_device_id(uint16_t device_id) {
    if (device_id >= 0x1000U && device_id <= 0x103FU) {
        return 1;
    }

    if (device_id >= 0x1040U && device_id <= 0x107FU) {
        return 1;
    }

    return 0;
}

static int virtio_is_block_device_id(uint16_t device_id) {
    return device_id == VIRTIO_PCI_BLOCK_DEVICE_ID ||
           device_id == VIRTIO_PCI_MODERN_BLOCK_DEVICE_ID;
}

static int virtio_is_modern_device_id(uint16_t device_id) {
    return device_id >= 0x1040U && device_id <= 0x107FU;
}

static int virtio_pci_match(const kernel_device_t* device) {
    if (device == 0 || device->bus_type != TERMOB_BUS_PCI) {
        return 0;
    }

    if (device->vendor_id != VIRTIO_PCI_VENDOR_ID) {
        return 0;
    }

    return virtio_is_supported_device_id(device->device_id);
}

static int virtio_logical_match(const kernel_device_t* device) {
    if (device == 0 || device->bus_type != TERMOB_BUS_VIRTIO) {
        return 0;
    }

    return device->vendor_id == VIRTIO_PCI_VENDOR_ID &&
           virtio_is_supported_device_id(device->device_id);
}

static int virtio_blk_match(const kernel_device_t* device) {
    if (!virtio_logical_match(device)) {
        return 0;
    }

    return virtio_is_block_device_id(device->device_id);
}

static char* virtio_build_child_name(uint16_t device_id,
                                     uint8_t bus,
                                     uint8_t slot,
                                     uint8_t function) {
    static const char hex_digits[] = "0123456789ABCDEF";
    const char* type_name;
    size_t prefix_length;
    char* name;
    size_t index;

    type_name = virtio_device_type_name(device_id);
    prefix_length = 0U;
    while (type_name[prefix_length] != '\0') {
        prefix_length++;
    }

    name = (char*)kmalloc(prefix_length + 8U);
    if (name == 0) {
        return 0;
    }

    virtio_copy_text(name, prefix_length + 8U, type_name);
    index = prefix_length;
    name[index++] = '@';
    name[index++] = hex_digits[(bus >> 4) & 0x0FU];
    name[index++] = hex_digits[bus & 0x0FU];
    name[index++] = ':';
    name[index++] = hex_digits[(slot >> 4) & 0x0FU];
    name[index++] = hex_digits[slot & 0x0FU];
    name[index++] = '.';
    name[index++] = hex_digits[function & 0x07U];
    name[index] = '\0';
    return name;
}

static void virtio_register_logical_device(const kernel_device_t* pci_device) {
    virtio_device_info_t* info;
    termob_device_t child;

    if (pci_device == 0) {
        return;
    }

    info = (virtio_device_info_t*)kmalloc(sizeof(virtio_device_info_t));
    if (info == 0) {
        virtio_log_event("TERMOB: virtio child registration skipped (no heap)");
        return;
    }

    info->transport_vendor_id = pci_device->vendor_id;
    info->transport_device_id = pci_device->device_id;
    info->transport_bus = pci_device->bus;
    info->transport_slot = pci_device->slot;
    info->transport_function = pci_device->function;

    child.name = virtio_build_child_name(pci_device->device_id,
                                         pci_device->bus,
                                         pci_device->slot,
                                         pci_device->function);
    if (child.name == 0) {
        child.name = "virtio-device";
    }
    child.bus_type = TERMOB_BUS_VIRTIO;
    child.bus_ref = (termob_bus_t*)driver_bus_by_type(TERMOB_BUS_VIRTIO);
    child.driver = 0;
    child.bound_driver_name = 0;
    child.driver_data = 0;
    child.data = info;
    child.vendor_id = pci_device->vendor_id;
    child.device_id = pci_device->device_id;
    child.class_code = pci_device->class_code;
    child.subclass = pci_device->subclass;
    child.prog_if = pci_device->prog_if;
    child.revision = pci_device->revision;
    child.bus = pci_device->bus;
    child.slot = pci_device->slot;
    child.function = pci_device->function;

    if (driver_register_device(&child)) {
        virtio_logical_device_count_value++;
        virtio_log_event("TERMOB: virtio logical device registered");
    }
}

static void virtio_pci_probe(kernel_device_t* device) {
    virtio_transport_bound_count_value++;
    virtio_log_event("TERMOB: virtio-pci device bound");
    virtio_register_logical_device(device);
}

static void virtio_blk_probe(kernel_device_t* device) {
    termob_block_device_t block_device;
    virtio_blk_state_t* state;

    state = (virtio_blk_state_t*)kmalloc(sizeof(virtio_blk_state_t));
    if (state == 0) {
        virtio_log_event("TERMOB: virtio-blk probe failed (no heap)");
        return;
    }

    state->transport = (const virtio_device_info_t*)device->data;
    state->capacity_sectors = 0U;
    state->block_size_bytes = 512U;
    state->io_base = 0U;
    state->queue_size = 0U;
    state->last_used_index = 0U;
    state->queue_memory = 0;
    state->queue_memory_bytes = 0U;
    state->desc_table = 0;
    state->avail_ring = 0;
    state->used_ring = 0;
    state->request_header = 0;
    state->request_status = 0;
    state->block_registered = 0;
    state->legacy_transport = 0;
    state->ready = 1;

    state->request_header = (virtio_blk_req_header_t*)kmalloc(sizeof(virtio_blk_req_header_t));
    state->request_status = (uint8_t*)kmalloc(1U);
    if (state->request_header == 0 || state->request_status == 0) {
        state->ready = 0;
        virtio_log_event("TERMOB: virtio-blk request allocation failed");
    }

    if (state->ready && !virtio_blk_setup_queue(state)) {
        state->ready = 0;
    }

    device->driver_data = state;
    virtio_blk_bound_count_value++;
    virtio_log_event(state->ready ? "TERMOB: virtio-blk device ready"
                                  : "TERMOB: virtio-blk device registered without read path");

    block_device.name = device->name;
    block_device.driver_name = "virtio-blk";
    block_device.sector_size_bytes = state->block_size_bytes;
    block_device.sector_count = state->capacity_sectors;
    block_device.read = virtio_blk_block_read;
    block_device.private_data = state;

    if (block_register_device(&block_device)) {
        state->block_registered = 1;
        virtio_log_event("TERMOB: virtio-blk block device registered");
    }
}

const char* virtio_device_type_name(uint16_t device_id) {
    switch (device_id) {
        case 0x1000U:
        case 0x1041U:
            return "virtio-net";
        case 0x1001U:
        case 0x1042U:
            return "virtio-block";
        case 0x1043U:
            return "virtio-console";
        case 0x1044U:
            return "virtio-rng";
        case 0x1045U:
            return "virtio-balloon";
        case 0x1048U:
            return "virtio-scsi";
        case 0x1049U:
            return "virtio-9p";
        case 0x1050U:
            return "virtio-gpu";
        case 0x1052U:
            return "virtio-input";
        case 0x1053U:
            return "virtio-vsock";
        default:
            if (device_id >= 0x1000U && device_id <= 0x103FU) {
                return "virtio-legacy";
            }
            if (device_id >= 0x1040U && device_id <= 0x107FU) {
                return "virtio-pci";
            }
            return "not-virtio";
    }
}

void virtio_init(void) {
    static const kernel_driver_t virtio_pci_driver = {
        "virtio-pci",
        TERMOB_BUS_PCI,
        virtio_pci_match,
        virtio_pci_probe,
        0,
        0
    };
    static const kernel_driver_t virtio_blk_driver = {
        "virtio-blk",
        TERMOB_BUS_VIRTIO,
        virtio_blk_match,
        virtio_blk_probe,
        0,
        0
    };

    if (virtio_ready) {
        return;
    }

    virtio_transport_bound_count_value = 0U;
    virtio_logical_device_count_value = 0U;
    virtio_blk_bound_count_value = 0U;

    if (!device_register_driver(&virtio_pci_driver)) {
        virtio_ready = 0;
        return;
    }

    virtio_ready = device_register_driver(&virtio_blk_driver);
}

int virtio_is_initialized(void) {
    return virtio_ready;
}

size_t virtio_bound_device_count(void) {
    return virtio_transport_bound_count_value;
}

size_t virtio_logical_device_count(void) {
    return virtio_logical_device_count_value;
}

size_t virtio_blk_bound_device_count(void) {
    return virtio_blk_bound_count_value;
}

int virtio_blk_read_first_sector(uint8_t* buffer, size_t buffer_size) {
    kernel_device_t device;
    size_t device_index;

    if (buffer == 0 || buffer_size < TERMOB_VIRTIO_BLK_SECTOR_SIZE) {
        return 0;
    }

    for (device_index = 0U; device_at(device_index, &device); device_index++) {
        virtio_blk_state_t* state;

        if (!virtio_blk_match(&device)) {
            continue;
        }

        state = (virtio_blk_state_t*)device.driver_data;
        if (state == 0 || !state->ready) {
            continue;
        }

        return virtio_blk_block_read(state, 0U, 1U, buffer);
    }

    return 0;
}

void virtio_dump_to_terminal(void) {
    kernel_device_t device;
    size_t device_index;
    size_t pci_devices;
    size_t logical_devices;

    terminal_writeline("Virtio subsystem:");
    terminal_write("  Driver     : ");
    terminal_writeline(virtio_ready ? "Registered" : "Offline");
    terminal_write("  PCI bound  : ");
    virtio_write_u32((uint32_t)virtio_transport_bound_count_value);
    terminal_writeline(" devices");
    terminal_write("  Logical    : ");
    virtio_write_u32((uint32_t)virtio_logical_device_count_value);
    terminal_writeline(" devices");
    terminal_write("  VBLK bound : ");
    virtio_write_u32((uint32_t)virtio_blk_bound_count_value);
    terminal_writeline(" devices");
    terminal_write("  Block devs : ");
    virtio_write_u32((uint32_t)block_device_count());
    terminal_writeline(" registered");

    pci_devices = 0U;
    logical_devices = 0U;
    for (device_index = 0U; device_at(device_index, &device); device_index++) {
        if (virtio_pci_match(&device)) {
            pci_devices++;
        }
        if (virtio_logical_match(&device)) {
            logical_devices++;
        }
    }

    terminal_write("  PCI seen   : ");
    virtio_write_u32((uint32_t)pci_devices);
    terminal_writeline(" devices");
    terminal_write("  Bus seen   : ");
    virtio_write_u32((uint32_t)logical_devices);
    terminal_writeline(" devices");

    if (pci_devices == 0U && logical_devices == 0U) {
        terminal_writeline("  No virtio devices detected");
        return;
    }

    terminal_writeline("Transport devices:");
    for (device_index = 0U; device_at(device_index, &device); device_index++) {
        if (!virtio_pci_match(&device)) {
            continue;
        }

        terminal_write("  ");
        virtio_write_hex8(device.bus);
        terminal_putchar(':');
        virtio_write_hex8(device.slot);
        terminal_putchar('.');
        virtio_write_hex8(device.function);
        terminal_write("  ");
        terminal_write(virtio_device_type_name(device.device_id));
        terminal_write("  ");
        virtio_write_hex16(device.vendor_id);
        terminal_putchar(':');
        virtio_write_hex16(device.device_id);
        if (device.bound_driver_name != 0) {
            terminal_write("  -> ");
            terminal_write(device.bound_driver_name);
        }
        terminal_putchar('\n');
    }

    terminal_writeline("Logical devices:");
    for (device_index = 0U; device_at(device_index, &device); device_index++) {
        if (!virtio_logical_match(&device)) {
            continue;
        }

        terminal_write("  ");
        terminal_write(device.name != 0 ? device.name : "virtio-device");
        terminal_write("  ");
        terminal_write(virtio_is_modern_device_id(device.device_id) ? "modern" : "legacy");
        if (device.bound_driver_name != 0) {
            terminal_write("  -> ");
            terminal_write(device.bound_driver_name);
        }
        terminal_putchar('\n');
    }
}

void virtio_blk_dump_to_terminal(void) {
    kernel_device_t device;
    size_t device_index;

    terminal_writeline("Virtio block devices:");
    terminal_write("  Driver     : ");
    terminal_writeline(virtio_ready ? "Registered" : "Offline");
    terminal_write("  Bound      : ");
    virtio_write_u32((uint32_t)virtio_blk_bound_count_value);
    terminal_writeline(" devices");

    if (virtio_blk_bound_count_value == 0U) {
        terminal_writeline("  No virtio block devices ready");
        terminal_writeline("  Tip        : run QEMU with -drive ... -device virtio-blk-pci,...");
        return;
    }

    for (device_index = 0U; device_at(device_index, &device); device_index++) {
        virtio_blk_state_t* state;

        if (!virtio_blk_match(&device)) {
            continue;
        }

        state = (virtio_blk_state_t*)device.driver_data;

        terminal_write("  ");
        terminal_write(device.name != 0 ? device.name : "virtio-block");
        terminal_write("  ");
        terminal_write(virtio_is_modern_device_id(device.device_id) ? "modern" : "legacy");
        terminal_write("  blk ");
        virtio_write_u32(state != 0 ? state->block_size_bytes : 512U);
        terminal_write("B");
        terminal_write("  read ");
        terminal_write((state != 0 && state->ready) ? "on" : "off");

        if (state != 0 && state->transport != 0) {
            terminal_write("  pci ");
            virtio_write_hex8(state->transport->transport_bus);
            terminal_putchar(':');
            virtio_write_hex8(state->transport->transport_slot);
            terminal_putchar('.');
            virtio_write_hex8(state->transport->transport_function);
            if (state->legacy_transport) {
                terminal_write("  io ");
                virtio_write_hex16(state->io_base);
            }
            terminal_write("  block ");
            terminal_writeline(state->block_registered ? "registered" : "pending");
            continue;
        }

        terminal_write("  block pending\n");
    }
}
