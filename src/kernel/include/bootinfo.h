#ifndef TERMOB_BOOTINFO_H
#define TERMOB_BOOTINFO_H

#include <stddef.h>
#include <stdint.h>

#define TERMOB_MULTIBOOT2_MAGIC 0x36D76289U

#define TERMOB_MEMORY_TYPE_AVAILABLE 1U
#define TERMOB_MEMORY_TYPE_RESERVED 2U
#define TERMOB_MEMORY_TYPE_ACPI_RECLAIMABLE 3U
#define TERMOB_MEMORY_TYPE_NVS 4U
#define TERMOB_MEMORY_TYPE_BADRAM 5U

typedef struct {
    uint32_t base_addr;
    uint32_t length;
    uint32_t type;
} bootinfo_memory_range_t;

void bootinfo_init(uint32_t multiboot_magic, uint32_t multiboot_info_addr);
int bootinfo_is_valid(void);
uint32_t bootinfo_multiboot_info_address(void);
uint32_t bootinfo_multiboot_info_size(void);
size_t bootinfo_memory_range_count(void);
int bootinfo_memory_range_at(size_t index, bootinfo_memory_range_t* out_range);
uint32_t bootinfo_usable_memory_bytes(void);
uint32_t bootinfo_highest_usable_address(void);

#endif
