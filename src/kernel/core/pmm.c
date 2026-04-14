#include "../include/pmm.h"

#include "../include/bootinfo.h"
#include "../include/heap.h"
#include "../include/kernel.h"

#define PMM_FRAME_SIZE 4096U

extern uint8_t __kernel_end;

static uint8_t* pmm_bitmap;
static uint32_t pmm_bitmap_size_bytes;
static uint32_t pmm_total_frames_count;
static uint32_t pmm_free_frames_count;
static uint32_t pmm_next_search_frame;
static int pmm_ready;

static uint32_t pmm_align_up_u32(uint32_t value, uint32_t alignment) {
    return (value + (alignment - 1U)) & ~(alignment - 1U);
}

static uint32_t pmm_align_down_u32(uint32_t value, uint32_t alignment) {
    return value & ~(alignment - 1U);
}

static uint32_t pmm_bitmap_index(uint32_t frame_index) {
    return frame_index >> 3;
}

static uint8_t pmm_bitmap_mask(uint32_t frame_index) {
    return (uint8_t)(1U << (frame_index & 7U));
}

static int pmm_frame_is_used(uint32_t frame_index) {
    return (pmm_bitmap[pmm_bitmap_index(frame_index)] & pmm_bitmap_mask(frame_index)) != 0U;
}

static void pmm_mark_frame_used(uint32_t frame_index) {
    if (frame_index >= pmm_total_frames_count || pmm_frame_is_used(frame_index)) {
        return;
    }

    pmm_bitmap[pmm_bitmap_index(frame_index)] |= pmm_bitmap_mask(frame_index);
    if (pmm_free_frames_count > 0U) {
        pmm_free_frames_count--;
    }
}

static void pmm_mark_frame_free(uint32_t frame_index) {
    if (frame_index >= pmm_total_frames_count || !pmm_frame_is_used(frame_index)) {
        return;
    }

    pmm_bitmap[pmm_bitmap_index(frame_index)] &= (uint8_t)~pmm_bitmap_mask(frame_index);
    pmm_free_frames_count++;
}

static void pmm_release_region(uint32_t base_addr, uint32_t length) {
    uint32_t start;
    uint32_t end;
    uint32_t frame_index;
    uint32_t start_frame;
    uint32_t end_frame;

    if (length == 0U) {
        return;
    }

    if (base_addr > UINT32_MAX - length) {
        end = UINT32_MAX;
    } else {
        end = base_addr + length;
    }

    start = pmm_align_up_u32(base_addr, PMM_FRAME_SIZE);
    end = pmm_align_down_u32(end, PMM_FRAME_SIZE);

    if (end <= start) {
        return;
    }

    start_frame = start / PMM_FRAME_SIZE;
    end_frame = end / PMM_FRAME_SIZE;

    for (frame_index = start_frame; frame_index < end_frame; frame_index++) {
        pmm_mark_frame_free(frame_index);
    }
}

static void pmm_reserve_region(uint32_t base_addr, uint32_t length) {
    uint32_t end;
    uint32_t frame_index;
    uint32_t start_frame;
    uint32_t end_frame;

    if (length == 0U) {
        return;
    }

    if (base_addr > UINT32_MAX - length) {
        end = UINT32_MAX;
    } else {
        end = base_addr + length;
    }

    start_frame = base_addr / PMM_FRAME_SIZE;
    end_frame = pmm_align_up_u32(end, PMM_FRAME_SIZE) / PMM_FRAME_SIZE;

    if (end_frame > pmm_total_frames_count) {
        end_frame = pmm_total_frames_count;
    }

    for (frame_index = start_frame; frame_index < end_frame; frame_index++) {
        pmm_mark_frame_used(frame_index);
    }
}

void pmm_init(void) {
    bootinfo_memory_range_t range;
    uint32_t highest_usable_address;
    uint32_t range_index;
    uint32_t bitmap_index;
    uint32_t kernel_reserved_end;

    pmm_bitmap = 0;
    pmm_bitmap_size_bytes = 0;
    pmm_total_frames_count = 0;
    pmm_free_frames_count = 0;
    pmm_next_search_frame = 0;
    pmm_ready = 0;

    if (!bootinfo_is_valid() || !heap_is_initialized()) {
        return;
    }

    highest_usable_address = bootinfo_highest_usable_address();
    if (highest_usable_address < (PMM_FRAME_SIZE - 1U)) {
        return;
    }

    if (highest_usable_address == UINT32_MAX) {
        pmm_total_frames_count = (UINT32_MAX / PMM_FRAME_SIZE) + 1U;
    } else {
        pmm_total_frames_count =
            pmm_align_up_u32(highest_usable_address + 1U, PMM_FRAME_SIZE) / PMM_FRAME_SIZE;
    }
    pmm_bitmap_size_bytes = (pmm_total_frames_count + 7U) / 8U;
    pmm_bitmap = (uint8_t*)kcalloc(pmm_bitmap_size_bytes, 1U);
    if (pmm_bitmap == 0) {
        pmm_bitmap_size_bytes = 0;
        pmm_total_frames_count = 0;
        return;
    }

    for (bitmap_index = 0; bitmap_index < pmm_bitmap_size_bytes; bitmap_index++) {
        pmm_bitmap[bitmap_index] = 0xFFU;
    }

    for (range_index = 0; bootinfo_memory_range_at(range_index, &range); range_index++) {
        if (range.type == TERMOB_MEMORY_TYPE_AVAILABLE) {
            pmm_release_region(range.base_addr, range.length);
        }
    }

    pmm_reserve_region(0U, TERMOB_KERNEL_LOAD_ADDRESS);

    kernel_reserved_end = pmm_align_up_u32((uint32_t)(uintptr_t)&__kernel_end, PMM_FRAME_SIZE);
    pmm_reserve_region(TERMOB_KERNEL_LOAD_ADDRESS, kernel_reserved_end - TERMOB_KERNEL_LOAD_ADDRESS);
    pmm_reserve_region(heap_start_address(), heap_end_address() - heap_start_address());
    pmm_reserve_region(bootinfo_multiboot_info_address(), bootinfo_multiboot_info_size());
    pmm_reserve_region((uint32_t)(uintptr_t)pmm_bitmap, pmm_bitmap_size_bytes);

    pmm_ready = 1;
}

int pmm_is_initialized(void) {
    return pmm_ready;
}

uint32_t pmm_alloc_frame(void) {
    uint32_t frame_index;

    if (!pmm_ready || pmm_free_frames_count == 0U) {
        return 0U;
    }

    for (frame_index = pmm_next_search_frame; frame_index < pmm_total_frames_count; frame_index++) {
        if (!pmm_frame_is_used(frame_index)) {
            pmm_mark_frame_used(frame_index);
            pmm_next_search_frame = frame_index + 1U;
            return frame_index * PMM_FRAME_SIZE;
        }
    }

    for (frame_index = 0; frame_index < pmm_next_search_frame; frame_index++) {
        if (!pmm_frame_is_used(frame_index)) {
            pmm_mark_frame_used(frame_index);
            pmm_next_search_frame = frame_index + 1U;
            return frame_index * PMM_FRAME_SIZE;
        }
    }

    return 0U;
}

void pmm_free_frame(uint32_t physical_address) {
    uint32_t frame_index;

    if (!pmm_ready || (physical_address & (PMM_FRAME_SIZE - 1U)) != 0U) {
        return;
    }

    frame_index = physical_address / PMM_FRAME_SIZE;
    if (frame_index >= pmm_total_frames_count) {
        return;
    }

    pmm_mark_frame_free(frame_index);
    if (frame_index < pmm_next_search_frame) {
        pmm_next_search_frame = frame_index;
    }
}

uint32_t pmm_frame_size(void) {
    return PMM_FRAME_SIZE;
}

uint32_t pmm_total_frames(void) {
    return pmm_total_frames_count;
}

uint32_t pmm_free_frames(void) {
    return pmm_free_frames_count;
}

uint32_t pmm_used_frames(void) {
    return pmm_total_frames_count - pmm_free_frames_count;
}

uint32_t pmm_total_bytes(void) {
    return pmm_total_frames_count * PMM_FRAME_SIZE;
}

uint32_t pmm_free_bytes(void) {
    return pmm_free_frames_count * PMM_FRAME_SIZE;
}

uint32_t pmm_used_bytes(void) {
    return (pmm_total_frames_count - pmm_free_frames_count) * PMM_FRAME_SIZE;
}

uint32_t pmm_bitmap_bytes(void) {
    return pmm_bitmap_size_bytes;
}
