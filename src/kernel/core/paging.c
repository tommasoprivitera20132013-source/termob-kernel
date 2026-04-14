#include "../include/paging.h"

#include "../include/bootinfo.h"
#include "../include/heap.h"
#include "../include/pmm.h"

#define PAGING_PAGE_SIZE 4096U
#define PAGING_TABLE_ENTRIES 1024U
#define PAGING_DIRECTORY_ENTRIES 1024U
#define PAGING_BYTES_PER_TABLE (PAGING_PAGE_SIZE * PAGING_TABLE_ENTRIES)
#define PAGING_MIN_IDENTITY_BYTES (16U * 1024U * 1024U)
#define PAGING_FLAG_PRESENT 0x001U
#define PAGING_FLAG_RW 0x002U

static uint32_t paging_directory_physical;
static uint32_t paging_identity_bytes;
static uint32_t paging_tables_count;
static int paging_ready;

static uint32_t paging_align_up_u32(uint32_t value, uint32_t alignment) {
    return (value + (alignment - 1U)) & ~(alignment - 1U);
}

static void paging_zero_page(uint32_t physical_address) {
    volatile uint32_t* page_words;
    uint32_t index;

    page_words = (volatile uint32_t*)(uintptr_t)physical_address;
    for (index = 0; index < (PAGING_PAGE_SIZE / sizeof(uint32_t)); index++) {
        page_words[index] = 0U;
    }
}

static uint32_t paging_identity_limit_bytes(void) {
    uint32_t limit;
    uint32_t bootinfo_end;

    limit = heap_end_address();
    if (limit < PAGING_MIN_IDENTITY_BYTES) {
        limit = PAGING_MIN_IDENTITY_BYTES;
    }

    if (bootinfo_is_valid()) {
        bootinfo_end = bootinfo_multiboot_info_address() + bootinfo_multiboot_info_size();
        if (bootinfo_end > limit) {
            limit = bootinfo_end;
        }
    }

    return paging_align_up_u32(limit, PAGING_BYTES_PER_TABLE);
}

void paging_init(void) {
    uint32_t* page_directory;
    uint32_t table_index;
    uint32_t page_index;
    uint32_t physical_address;

    paging_directory_physical = 0U;
    paging_identity_bytes = 0U;
    paging_tables_count = 0U;
    paging_ready = 0;

    if (!pmm_is_initialized() || !heap_is_initialized()) {
        return;
    }

    paging_identity_bytes = paging_identity_limit_bytes();
    paging_tables_count = paging_identity_bytes / PAGING_BYTES_PER_TABLE;
    if (paging_tables_count == 0U || paging_tables_count > PAGING_DIRECTORY_ENTRIES) {
        paging_identity_bytes = 0U;
        paging_tables_count = 0U;
        return;
    }

    paging_directory_physical = pmm_alloc_frame();
    if (paging_directory_physical == 0U) {
        paging_identity_bytes = 0U;
        paging_tables_count = 0U;
        return;
    }

    paging_zero_page(paging_directory_physical);
    page_directory = (uint32_t*)(uintptr_t)paging_directory_physical;

    physical_address = 0U;
    for (table_index = 0; table_index < paging_tables_count; table_index++) {
        uint32_t table_physical;
        uint32_t* page_table;

        table_physical = pmm_alloc_frame();
        if (table_physical == 0U) {
            paging_directory_physical = 0U;
            paging_identity_bytes = 0U;
            paging_tables_count = 0U;
            return;
        }

        paging_zero_page(table_physical);
        page_table = (uint32_t*)(uintptr_t)table_physical;

        for (page_index = 0; page_index < PAGING_TABLE_ENTRIES; page_index++) {
            if (physical_address < paging_identity_bytes) {
                page_table[page_index] = physical_address | PAGING_FLAG_PRESENT | PAGING_FLAG_RW;
                physical_address += PAGING_PAGE_SIZE;
            } else {
                break;
            }
        }

        page_directory[table_index] = table_physical | PAGING_FLAG_PRESENT | PAGING_FLAG_RW;
    }

    __asm__ volatile ("mov %0, %%cr3" : : "r"(paging_directory_physical) : "memory");
    __asm__ volatile (
        "mov %%cr0, %%eax\n\t"
        "or $0x80000000, %%eax\n\t"
        "mov %%eax, %%cr0\n\t"
        "jmp 1f\n\t"
        "1:\n\t"
        :
        :
        : "eax", "memory");

    paging_ready = 1;
}

int paging_is_enabled(void) {
    return paging_ready;
}

uint32_t paging_directory_physical_address(void) {
    return paging_directory_physical;
}

uint32_t paging_identity_mapped_bytes(void) {
    return paging_identity_bytes;
}

uint32_t paging_table_count(void) {
    return paging_tables_count;
}

uint32_t paging_page_size_bytes(void) {
    return PAGING_PAGE_SIZE;
}
