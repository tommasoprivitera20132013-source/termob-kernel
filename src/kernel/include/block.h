#ifndef TERMOB_BLOCK_H
#define TERMOB_BLOCK_H

#include <stddef.h>
#include <stdint.h>

typedef struct termob_block_device {
    const char* name;
    const char* driver_name;
    uint32_t sector_size_bytes;
    uint32_t sector_count;
    int (*read)(void* private_data,
                uint32_t lba,
                uint32_t sector_count,
                void* buffer);
    void* private_data;
} termob_block_device_t;

void block_init(void);
int block_is_initialized(void);
int block_register_device(const termob_block_device_t* device);
size_t block_device_count(void);
int block_device_at(size_t index, termob_block_device_t* out_device);
int block_read_device(size_t index,
                      uint32_t lba,
                      uint32_t sector_count,
                      void* buffer);
void block_dump_to_terminal(void);

#endif
