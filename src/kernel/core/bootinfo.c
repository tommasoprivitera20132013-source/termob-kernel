#include "../include/bootinfo.h"

#define TERMOB_BOOTINFO_MAX_MEMORY_RANGES 64U
#define MULTIBOOT_TAG_TYPE_END 0U
#define MULTIBOOT_TAG_TYPE_MMAP 6U

typedef struct {
    uint32_t total_size;
    uint32_t reserved;
} multiboot_info_header_t;

typedef struct {
    uint32_t type;
    uint32_t size;
} multiboot_tag_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
} multiboot_tag_mmap_t;

typedef struct {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t zero;
} __attribute__((packed)) multiboot_mmap_entry_t;

static int bootinfo_ready;
static uint32_t bootinfo_info_address;
static uint32_t bootinfo_info_size;
static bootinfo_memory_range_t bootinfo_memory_ranges[TERMOB_BOOTINFO_MAX_MEMORY_RANGES];
static size_t bootinfo_memory_range_count_value;
static uint32_t bootinfo_usable_memory_bytes_value;
static uint32_t bootinfo_highest_usable_address_value;

static uint32_t bootinfo_align_up_u32(uint32_t value, uint32_t alignment) {
    return (value + (alignment - 1U)) & ~(alignment - 1U);
}

static void bootinfo_reset(void) {
    bootinfo_ready = 0;
    bootinfo_info_address = 0;
    bootinfo_info_size = 0;
    bootinfo_memory_range_count_value = 0;
    bootinfo_usable_memory_bytes_value = 0;
    bootinfo_highest_usable_address_value = 0;
}

static void bootinfo_store_memory_range(const multiboot_mmap_entry_t* entry) {
    uint64_t end_address;
    uint64_t capped_end_address;
    uint32_t base_addr;
    uint32_t length;

    if (bootinfo_memory_range_count_value >= TERMOB_BOOTINFO_MAX_MEMORY_RANGES) {
        return;
    }

    if (entry->len == 0U || entry->addr >= 0x100000000ULL) {
        return;
    }

    end_address = entry->addr + entry->len;
    if (end_address < entry->addr || end_address > 0x100000000ULL) {
        capped_end_address = 0x100000000ULL;
    } else {
        capped_end_address = end_address;
    }

    base_addr = (uint32_t)entry->addr;
    length = (uint32_t)(capped_end_address - entry->addr);
    if (length == 0U) {
        return;
    }

    bootinfo_memory_ranges[bootinfo_memory_range_count_value].base_addr = base_addr;
    bootinfo_memory_ranges[bootinfo_memory_range_count_value].length = length;
    bootinfo_memory_ranges[bootinfo_memory_range_count_value].type = entry->type;
    bootinfo_memory_range_count_value++;

    if (entry->type == TERMOB_MEMORY_TYPE_AVAILABLE) {
        if (bootinfo_usable_memory_bytes_value > UINT32_MAX - length) {
            bootinfo_usable_memory_bytes_value = UINT32_MAX;
        } else {
            bootinfo_usable_memory_bytes_value += length;
        }

        if ((uint32_t)(capped_end_address - 1U) > bootinfo_highest_usable_address_value) {
            bootinfo_highest_usable_address_value = (uint32_t)(capped_end_address - 1U);
        }
    }
}

void bootinfo_init(uint32_t multiboot_magic, uint32_t multiboot_info_addr) {
    const multiboot_info_header_t* header;
    const multiboot_tag_t* tag;
    uint32_t info_end;

    bootinfo_reset();

    if (multiboot_magic != TERMOB_MULTIBOOT2_MAGIC || multiboot_info_addr == 0U) {
        return;
    }

    header = (const multiboot_info_header_t*)(uintptr_t)multiboot_info_addr;
    if (header->total_size < sizeof(multiboot_info_header_t)) {
        return;
    }

    bootinfo_info_address = multiboot_info_addr;
    bootinfo_info_size = header->total_size;
    info_end = multiboot_info_addr + header->total_size;
    tag = (const multiboot_tag_t*)(uintptr_t)(multiboot_info_addr + 8U);

    while ((uint32_t)(uintptr_t)tag + sizeof(multiboot_tag_t) <= info_end) {
        if (tag->type == MULTIBOOT_TAG_TYPE_END) {
            break;
        }

        if (tag->type == MULTIBOOT_TAG_TYPE_MMAP) {
            const multiboot_tag_mmap_t* mmap_tag;
            const uint8_t* entry_cursor;
            const uint8_t* tag_end;

            mmap_tag = (const multiboot_tag_mmap_t*)tag;
            if (mmap_tag->entry_size >= sizeof(multiboot_mmap_entry_t)) {
                entry_cursor = (const uint8_t*)mmap_tag + sizeof(multiboot_tag_mmap_t);
                tag_end = (const uint8_t*)mmap_tag + mmap_tag->size;

                while (entry_cursor + mmap_tag->entry_size <= tag_end) {
                    const multiboot_mmap_entry_t* entry;

                    entry = (const multiboot_mmap_entry_t*)(const void*)entry_cursor;
                    bootinfo_store_memory_range(entry);
                    entry_cursor += mmap_tag->entry_size;
                }
            }
        }

        if (tag->size < sizeof(multiboot_tag_t)) {
            break;
        }

        tag = (const multiboot_tag_t*)(uintptr_t)(
            bootinfo_align_up_u32((uint32_t)(uintptr_t)tag + tag->size, 8U));
    }

    if (bootinfo_memory_range_count_value != 0U) {
        bootinfo_ready = 1;
    }
}

int bootinfo_is_valid(void) {
    return bootinfo_ready;
}

uint32_t bootinfo_multiboot_info_address(void) {
    return bootinfo_info_address;
}

uint32_t bootinfo_multiboot_info_size(void) {
    return bootinfo_info_size;
}

size_t bootinfo_memory_range_count(void) {
    return bootinfo_memory_range_count_value;
}

int bootinfo_memory_range_at(size_t index, bootinfo_memory_range_t* out_range) {
    if (!bootinfo_ready || out_range == 0 || index >= bootinfo_memory_range_count_value) {
        return 0;
    }

    *out_range = bootinfo_memory_ranges[index];
    return 1;
}

uint32_t bootinfo_usable_memory_bytes(void) {
    return bootinfo_usable_memory_bytes_value;
}

uint32_t bootinfo_highest_usable_address(void) {
    return bootinfo_highest_usable_address_value;
}
