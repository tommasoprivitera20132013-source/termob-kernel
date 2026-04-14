#ifndef TERMOB_VIRTIO_H
#define TERMOB_VIRTIO_H

#include <stddef.h>
#include <stdint.h>

void virtio_init(void);
int virtio_is_initialized(void);
size_t virtio_bound_device_count(void);
size_t virtio_logical_device_count(void);
size_t virtio_blk_bound_device_count(void);
const char* virtio_device_type_name(uint16_t device_id);
void virtio_dump_to_terminal(void);
void virtio_blk_dump_to_terminal(void);
int virtio_blk_read_first_sector(uint8_t* buffer, size_t buffer_size);

#endif
