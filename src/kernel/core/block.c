#include "../include/block.h"

#include "../include/heap.h"
#include "../include/klog.h"
#include "../include/serial.h"
#include "../include/terminal.h"

typedef struct block_device_node {
    termob_block_device_t device;
    struct block_device_node* next;
} block_device_node_t;

static int block_ready;
static block_device_node_t* block_device_list;
static size_t block_device_count_value;

static void block_write_u32(uint32_t value) {
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

static void block_log_event(const char* message) {
    klog_writeline(message);
    serial_writeline(message);
}

void block_init(void) {
    block_ready = 0;
    block_device_list = 0;
    block_device_count_value = 0U;

    if (!heap_is_initialized()) {
        return;
    }

    block_ready = 1;
}

int block_is_initialized(void) {
    return block_ready;
}

int block_register_device(const termob_block_device_t* device) {
    block_device_node_t* node;

    if (!block_ready || device == 0 || device->name == 0) {
        return 0;
    }

    node = (block_device_node_t*)kmalloc(sizeof(block_device_node_t));
    if (node == 0) {
        return 0;
    }

    node->device = *device;
    node->next = 0;

    if (block_device_list == 0) {
        block_device_list = node;
    } else {
        block_device_node_t* tail;

        tail = block_device_list;
        while (tail->next != 0) {
            tail = tail->next;
        }
        tail->next = node;
    }

    block_device_count_value++;
    block_log_event("TERMOB: block device registered");
    return 1;
}

size_t block_device_count(void) {
    return block_device_count_value;
}

int block_device_at(size_t index, termob_block_device_t* out_device) {
    block_device_node_t* node;
    size_t cursor;

    if (!block_ready || out_device == 0) {
        return 0;
    }

    node = block_device_list;
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

int block_read_device(size_t index,
                      uint32_t lba,
                      uint32_t sector_count,
                      void* buffer) {
    termob_block_device_t device;
    uint8_t* bytes;
    uint32_t sector_index;

    if (!block_device_at(index, &device) || device.read == 0 || buffer == 0 || sector_count == 0U) {
        return 0;
    }

    if (sector_count == 1U) {
        return device.read(device.private_data, lba, sector_count, buffer);
    }

    if (device.sector_size_bytes == 0U) {
        return 0;
    }

    bytes = (uint8_t*)buffer;
    for (sector_index = 0U; sector_index < sector_count; sector_index++) {
        if (!device.read(device.private_data,
                         lba + sector_index,
                         1U,
                         bytes + ((size_t)sector_index * device.sector_size_bytes))) {
            return 0;
        }
    }

    return 1;
}

void block_dump_to_terminal(void) {
    termob_block_device_t device;
    size_t index;

    terminal_writeline("Block devices:");
    terminal_write("  State      : ");
    terminal_writeline(block_ready ? "Ready" : "Offline");
    terminal_write("  Devices    : ");
    block_write_u32((uint32_t)block_device_count_value);
    terminal_writeline(" registered");

    if (block_device_count_value == 0U) {
        terminal_writeline("  No block devices available");
        return;
    }

    for (index = 0U; block_device_at(index, &device); index++) {
        terminal_write("  #");
        block_write_u32((uint32_t)index);
        terminal_write("  ");
        terminal_write(device.name);
        terminal_write("  drv ");
        terminal_write(device.driver_name != 0 ? device.driver_name : "unknown");
        terminal_write("  sector ");
        block_write_u32(device.sector_size_bytes);
        terminal_write("B");

        if (device.sector_count != 0U) {
            terminal_write("  count ");
            block_write_u32(device.sector_count);
        } else {
            terminal_write("  count unknown");
        }

        terminal_write("  read ");
        terminal_writeline(device.read != 0 ? "hooked" : "offline");
    }
}
