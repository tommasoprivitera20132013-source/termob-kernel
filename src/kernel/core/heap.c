#include "../include/heap.h"

#include "../include/klog.h"
#include "../include/serial.h"

#define TERMOB_HEAP_ALIGNMENT 16U
#define TERMOB_HEAP_SIZE_BYTES (1024U * 1024U)
#define TERMOB_HEAP_BLOCK_MAGIC 0x48454150U
#define TERMOB_HEAP_BLOCK_FLAG_FREE 0x00000001U
#define TERMOB_HEAP_MIN_SPLIT_BYTES TERMOB_HEAP_ALIGNMENT
#define TERMOB_HEAP_DIAG_INVALID_FREE 0x00000001U
#define TERMOB_HEAP_DIAG_DOUBLE_FREE 0x00000002U
#define TERMOB_HEAP_DIAG_CORRUPTION 0x00000004U
#define TERMOB_HEAP_DIAG_INTEGRITY 0x00000008U
#define TERMOB_HEAP_DIAG_SELFTEST 0x00000010U

typedef struct heap_block heap_block_t;

struct heap_block {
    uint32_t magic;
    uint32_t flags;
    size_t size_bytes;
    heap_block_t* prev_phys;
    heap_block_t* next_phys;
    heap_block_t* prev_free;
    heap_block_t* next_free;
};

#define TERMOB_HEAP_HEADER_SIZE \
    ((sizeof(heap_block_t) + (TERMOB_HEAP_ALIGNMENT - 1U)) & ~(TERMOB_HEAP_ALIGNMENT - 1U))

extern uint8_t __kernel_end;

static uintptr_t heap_start_ptr;
static uintptr_t heap_end_ptr;
static heap_block_t* heap_first_block;
static heap_block_t* heap_free_list;
static int heap_ready;
static uint32_t heap_diag_error_count;
static uint32_t heap_diag_error_flags;
static const char* heap_diag_last_error_message = "none";

static uintptr_t heap_align_up(uintptr_t value, uintptr_t alignment) {
    return (value + (alignment - 1U)) & ~(alignment - 1U);
}

static void heap_log_event(const char* message) {
    klog_writeline(message);
    serial_writeline(message);
}

static void heap_note_error(uint32_t flag, const char* message) {
    heap_diag_error_count++;
    heap_diag_error_flags |= flag;
    heap_diag_last_error_message = message;
    heap_log_event(message);
}

static uintptr_t heap_block_payload_address(const heap_block_t* block) {
    return (uintptr_t)block + (uintptr_t)TERMOB_HEAP_HEADER_SIZE;
}

static uintptr_t heap_block_end_address(const heap_block_t* block) {
    return heap_block_payload_address(block) + (uintptr_t)block->size_bytes;
}

static int heap_block_is_free(const heap_block_t* block) {
    return (block->flags & TERMOB_HEAP_BLOCK_FLAG_FREE) != 0U;
}

static void heap_mark_block_free(heap_block_t* block) {
    block->flags |= TERMOB_HEAP_BLOCK_FLAG_FREE;
}

static void heap_mark_block_used(heap_block_t* block) {
    block->flags &= (uint32_t)~TERMOB_HEAP_BLOCK_FLAG_FREE;
}

static void heap_zero_bytes(void* dst, size_t count) {
    uint8_t* bytes;
    size_t index;

    bytes = (uint8_t*)dst;
    for (index = 0U; index < count; index++) {
        bytes[index] = 0U;
    }
}

static size_t heap_total_payload_bytes(void) {
    if (!heap_ready || heap_end_ptr <= heap_start_ptr + TERMOB_HEAP_HEADER_SIZE) {
        return 0U;
    }

    return (size_t)((heap_end_ptr - heap_start_ptr) - TERMOB_HEAP_HEADER_SIZE);
}

static int heap_pointer_within_region(uintptr_t pointer) {
    return pointer >= heap_start_ptr && pointer < heap_end_ptr;
}

static size_t heap_max_walks(void) {
    size_t total_bytes;

    total_bytes = (size_t)(heap_end_ptr - heap_start_ptr);
    if (total_bytes == 0U) {
        return 0U;
    }

    return (total_bytes / TERMOB_HEAP_ALIGNMENT) + 1U;
}

static int heap_block_is_sane(const heap_block_t* block) {
    uintptr_t block_address;
    uintptr_t payload_address;
    uintptr_t block_end;

    if (block == 0) {
        return 0;
    }

    block_address = (uintptr_t)block;
    payload_address = heap_block_payload_address(block);
    block_end = heap_block_end_address(block);

    if ((block_address & (TERMOB_HEAP_ALIGNMENT - 1U)) != 0U) {
        return 0;
    }

    if (!heap_pointer_within_region(block_address) || payload_address < block_address) {
        return 0;
    }

    if (block->magic != TERMOB_HEAP_BLOCK_MAGIC) {
        return 0;
    }

    if ((block->size_bytes & (TERMOB_HEAP_ALIGNMENT - 1U)) != 0U) {
        return 0;
    }

    if (block_end < payload_address || block_end > heap_end_ptr) {
        return 0;
    }

    return 1;
}

static void heap_remove_free_block(heap_block_t* block) {
    if (block == 0) {
        return;
    }

    if (block->prev_free != 0) {
        block->prev_free->next_free = block->next_free;
    } else if (heap_free_list == block) {
        heap_free_list = block->next_free;
    }

    if (block->next_free != 0) {
        block->next_free->prev_free = block->prev_free;
    }

    block->prev_free = 0;
    block->next_free = 0;
}

static void heap_insert_free_block(heap_block_t* block) {
    heap_block_t* cursor;
    heap_block_t* previous;

    if (block == 0) {
        return;
    }

    heap_mark_block_free(block);
    block->prev_free = 0;
    block->next_free = 0;

    if (heap_free_list == 0 || (uintptr_t)block < (uintptr_t)heap_free_list) {
        block->next_free = heap_free_list;
        if (heap_free_list != 0) {
            heap_free_list->prev_free = block;
        }
        heap_free_list = block;
        return;
    }

    previous = heap_free_list;
    cursor = heap_free_list->next_free;
    while (cursor != 0 && (uintptr_t)cursor < (uintptr_t)block) {
        previous = cursor;
        cursor = cursor->next_free;
    }

    block->prev_free = previous;
    block->next_free = cursor;
    previous->next_free = block;
    if (cursor != 0) {
        cursor->prev_free = block;
    }
}

static void heap_initialize_block(heap_block_t* block,
                                  size_t size_bytes,
                                  heap_block_t* prev_phys,
                                  heap_block_t* next_phys,
                                  int is_free) {
    block->magic = TERMOB_HEAP_BLOCK_MAGIC;
    block->flags = 0U;
    block->size_bytes = size_bytes;
    block->prev_phys = prev_phys;
    block->next_phys = next_phys;
    block->prev_free = 0;
    block->next_free = 0;

    if (is_free) {
        heap_mark_block_free(block);
    }
}

static int heap_can_split_block(const heap_block_t* block, size_t requested_size) {
    size_t remaining_bytes;

    if (block == 0 || requested_size > block->size_bytes) {
        return 0;
    }

    remaining_bytes = block->size_bytes - requested_size;
    return remaining_bytes >= (TERMOB_HEAP_HEADER_SIZE + TERMOB_HEAP_MIN_SPLIT_BYTES);
}

static heap_block_t* heap_split_block(heap_block_t* block, size_t requested_size) {
    heap_block_t* split_block;
    uintptr_t split_address;
    size_t split_payload_bytes;

    if (!heap_can_split_block(block, requested_size)) {
        return 0;
    }

    split_address = heap_block_payload_address(block) + (uintptr_t)requested_size;
    split_block = (heap_block_t*)split_address;
    split_payload_bytes = block->size_bytes - requested_size - TERMOB_HEAP_HEADER_SIZE;

    heap_initialize_block(split_block, split_payload_bytes, block, block->next_phys, 1);
    if (block->next_phys != 0) {
        block->next_phys->prev_phys = split_block;
    }

    block->next_phys = split_block;
    block->size_bytes = requested_size;
    return split_block;
}

static heap_block_t* heap_find_block_by_payload(const void* ptr) {
    heap_block_t* block;
    size_t walks;

    if (!heap_ready || ptr == 0) {
        return 0;
    }

    block = heap_first_block;
    walks = 0U;
    while (block != 0 && walks < heap_max_walks()) {
        if (!heap_block_is_sane(block)) {
            heap_note_error(TERMOB_HEAP_DIAG_CORRUPTION,
                            "TERMOB: heap corruption detected during free lookup");
            return 0;
        }

        if (heap_block_payload_address(block) == (uintptr_t)ptr) {
            return block;
        }

        block = block->next_phys;
        walks++;
    }

    return 0;
}

static int heap_free_list_contains(const heap_block_t* target) {
    heap_block_t* block;
    size_t walks;

    if (target == 0) {
        return 0;
    }

    block = heap_free_list;
    walks = 0U;
    while (block != 0) {
        if (walks++ >= heap_max_walks()) {
            return 0;
        }

        if (block == target) {
            return 1;
        }

        block = block->next_free;
    }

    return 0;
}

static void heap_try_coalesce_with_next(heap_block_t* block) {
    heap_block_t* next;

    if (block == 0 || !heap_block_is_free(block)) {
        return;
    }

    next = block->next_phys;
    if (next == 0 || !heap_block_is_free(next)) {
        return;
    }

        if (!heap_block_is_sane(next) ||
            heap_block_end_address(block) != (uintptr_t)next) {
        heap_note_error(TERMOB_HEAP_DIAG_CORRUPTION,
                        "TERMOB: heap corruption detected during coalesce");
        return;
    }

    heap_remove_free_block(next);
    block->size_bytes += TERMOB_HEAP_HEADER_SIZE + next->size_bytes;
    block->next_phys = next->next_phys;
    if (next->next_phys != 0) {
        next->next_phys->prev_phys = block;
    }
}

static heap_block_t* heap_coalesce(heap_block_t* block) {
    if (block == 0) {
        return 0;
    }

    if (block->prev_phys != 0 && heap_block_is_free(block->prev_phys)) {
        block = block->prev_phys;
    }

    heap_try_coalesce_with_next(block);
    heap_try_coalesce_with_next(block);
    return block;
}

static int heap_check_integrity_internal(int log_errors) {
    heap_block_t* block;
    heap_block_t* previous;
    heap_block_t* free_block;
    uintptr_t expected_address;
    size_t physical_free_blocks;
    size_t free_list_blocks;
    size_t walks;

    if (!heap_ready || heap_first_block == 0) {
        return 0;
    }

    block = heap_first_block;
    previous = 0;
    expected_address = heap_start_ptr;
    physical_free_blocks = 0U;
    walks = 0U;

    while (block != 0) {
        if (walks++ >= heap_max_walks()) {
            if (log_errors) {
                heap_note_error(TERMOB_HEAP_DIAG_INTEGRITY,
                                "TERMOB: heap integrity failed (physical loop)");
            }
            return 0;
        }

        if (!heap_block_is_sane(block)) {
            if (log_errors) {
                heap_note_error(TERMOB_HEAP_DIAG_INTEGRITY,
                                "TERMOB: heap integrity failed (invalid block)");
            }
            return 0;
        }

        if ((uintptr_t)block != expected_address) {
            if (log_errors) {
                heap_note_error(TERMOB_HEAP_DIAG_INTEGRITY,
                                "TERMOB: heap integrity failed (non-contiguous block chain)");
            }
            return 0;
        }

        if (block->prev_phys != previous) {
            if (log_errors) {
                heap_note_error(TERMOB_HEAP_DIAG_INTEGRITY,
                                "TERMOB: heap integrity failed (broken prev_phys)");
            }
            return 0;
        }

        if (heap_block_is_free(block)) {
            physical_free_blocks++;
            if (block->next_phys != 0 && heap_block_is_free(block->next_phys)) {
                if (log_errors) {
                    heap_note_error(
                        TERMOB_HEAP_DIAG_INTEGRITY,
                        "TERMOB: heap integrity failed (adjacent free blocks not coalesced)");
                }
                return 0;
            }
        }

        previous = block;
        expected_address = heap_block_end_address(block);
        block = block->next_phys;
    }

    if (expected_address != heap_end_ptr) {
        if (log_errors) {
            heap_note_error(TERMOB_HEAP_DIAG_INTEGRITY,
                            "TERMOB: heap integrity failed (heap end mismatch)");
        }
        return 0;
    }

    free_block = heap_free_list;
    previous = 0;
    free_list_blocks = 0U;
    walks = 0U;
    while (free_block != 0) {
        if (walks++ >= heap_max_walks()) {
            if (log_errors) {
                heap_note_error(TERMOB_HEAP_DIAG_INTEGRITY,
                                "TERMOB: heap integrity failed (free list loop)");
            }
            return 0;
        }

        if (!heap_block_is_sane(free_block) || !heap_block_is_free(free_block)) {
            if (log_errors) {
                heap_note_error(TERMOB_HEAP_DIAG_INTEGRITY,
                                "TERMOB: heap integrity failed (invalid free block)");
            }
            return 0;
        }

        if (free_block->prev_free != previous) {
            if (log_errors) {
                heap_note_error(TERMOB_HEAP_DIAG_INTEGRITY,
                                "TERMOB: heap integrity failed (broken prev_free)");
            }
            return 0;
        }

        if (previous != 0 && (uintptr_t)previous >= (uintptr_t)free_block) {
            if (log_errors) {
                heap_note_error(TERMOB_HEAP_DIAG_INTEGRITY,
                                "TERMOB: heap integrity failed (free list ordering)");
            }
            return 0;
        }

        free_list_blocks++;
        previous = free_block;
        free_block = free_block->next_free;
    }

    if (free_list_blocks != physical_free_blocks) {
        if (log_errors) {
            heap_note_error(TERMOB_HEAP_DIAG_INTEGRITY,
                            "TERMOB: heap integrity failed (free block count mismatch)");
        }
        return 0;
    }

    block = heap_first_block;
    walks = 0U;
    while (block != 0) {
        if (walks++ >= heap_max_walks()) {
            if (log_errors) {
                heap_note_error(TERMOB_HEAP_DIAG_INTEGRITY,
                                "TERMOB: heap integrity failed (physical rewalk loop)");
            }
            return 0;
        }

        if (heap_block_is_free(block) && !heap_free_list_contains(block)) {
            if (log_errors) {
                heap_note_error(TERMOB_HEAP_DIAG_INTEGRITY,
                                "TERMOB: heap integrity failed (free block missing from free list)");
            }
            return 0;
        }

        block = block->next_phys;
    }

    return 1;
}

void heap_init(void) {
    size_t initial_payload_bytes;

    heap_start_ptr = heap_align_up((uintptr_t)&__kernel_end, TERMOB_HEAP_ALIGNMENT);
    heap_end_ptr = heap_start_ptr + TERMOB_HEAP_SIZE_BYTES;
    heap_first_block = 0;
    heap_free_list = 0;
    heap_ready = 0;
    heap_diag_error_count = 0U;
    heap_diag_error_flags = 0U;
    heap_diag_last_error_message = "none";

    if (heap_end_ptr <= heap_start_ptr + TERMOB_HEAP_HEADER_SIZE) {
        return;
    }

    initial_payload_bytes = (size_t)((heap_end_ptr - heap_start_ptr) - TERMOB_HEAP_HEADER_SIZE);
    initial_payload_bytes =
        (size_t)heap_align_up((uintptr_t)initial_payload_bytes, TERMOB_HEAP_ALIGNMENT);
    if (heap_start_ptr + TERMOB_HEAP_HEADER_SIZE + initial_payload_bytes > heap_end_ptr) {
        initial_payload_bytes -= TERMOB_HEAP_ALIGNMENT;
    }

    heap_first_block = (heap_block_t*)heap_start_ptr;
    heap_initialize_block(heap_first_block, initial_payload_bytes, 0, 0, 1);
    heap_free_list = heap_first_block;
    heap_ready = 1;
}

int heap_is_initialized(void) {
    return heap_ready;
}

void* kmalloc(size_t size) {
    heap_block_t* block;
    heap_block_t* split_block;
    size_t aligned_size;

    if (!heap_ready || size == 0U) {
        return 0;
    }

    aligned_size = (size_t)heap_align_up((uintptr_t)size, TERMOB_HEAP_ALIGNMENT);
    block = heap_free_list;
    while (block != 0) {
        if (!heap_block_is_sane(block) || !heap_block_is_free(block)) {
            heap_note_error(TERMOB_HEAP_DIAG_CORRUPTION,
                            "TERMOB: heap corruption detected during allocation");
            return 0;
        }

        if (block->size_bytes >= aligned_size) {
            heap_remove_free_block(block);
            split_block = heap_split_block(block, aligned_size);
            if (split_block != 0) {
                heap_insert_free_block(split_block);
            }

            heap_mark_block_used(block);
            return (void*)heap_block_payload_address(block);
        }

        block = block->next_free;
    }

    return 0;
}

void* kcalloc(size_t count, size_t size) {
    size_t total_size;
    void* block;

    if (count == 0U || size == 0U) {
        return 0;
    }

    if (count > ((size_t)-1) / size) {
        return 0;
    }

    total_size = count * size;
    block = kmalloc(total_size);
    if (block == 0) {
        return 0;
    }

    heap_zero_bytes(block, total_size);
    return block;
}

void kfree(void* ptr) {
    heap_block_t* block;

    if (!heap_ready || ptr == 0) {
        return;
    }

    block = heap_find_block_by_payload(ptr);
    if (block == 0) {
        heap_note_error(TERMOB_HEAP_DIAG_INVALID_FREE, "TERMOB: invalid kfree pointer");
        return;
    }

    if (heap_block_is_free(block)) {
        heap_note_error(TERMOB_HEAP_DIAG_DOUBLE_FREE, "TERMOB: double free detected");
        return;
    }

    heap_insert_free_block(block);
    heap_coalesce(block);
}

int heap_integrity_status(void) {
    return heap_check_integrity_internal(0);
}

int heap_check_integrity(void) {
    return heap_check_integrity_internal(1);
}

size_t heap_total_block_count(void) {
    heap_block_t* block;
    size_t count;
    size_t walks;

    if (!heap_ready) {
        return 0U;
    }

    count = 0U;
    walks = 0U;
    block = heap_first_block;
    while (block != 0 && walks < heap_max_walks()) {
        if (!heap_block_is_sane(block)) {
            return 0U;
        }

        count++;
        block = block->next_phys;
        walks++;
    }

    return count;
}

size_t heap_used_block_count(void) {
    heap_block_t* block;
    size_t count;
    size_t walks;

    if (!heap_ready) {
        return 0U;
    }

    count = 0U;
    walks = 0U;
    block = heap_first_block;
    while (block != 0 && walks < heap_max_walks()) {
        if (!heap_block_is_sane(block)) {
            return 0U;
        }

        if (!heap_block_is_free(block)) {
            count++;
        }

        block = block->next_phys;
        walks++;
    }

    return count;
}

size_t heap_free_block_count(void) {
    heap_block_t* block;
    size_t count;
    size_t walks;

    if (!heap_ready) {
        return 0U;
    }

    count = 0U;
    walks = 0U;
    block = heap_free_list;
    while (block != 0 && walks < heap_max_walks()) {
        count++;
        block = block->next_free;
        walks++;
    }

    return count;
}

size_t heap_largest_free_block(void) {
    heap_block_t* block;
    size_t largest;
    size_t walks;

    if (!heap_ready) {
        return 0U;
    }

    largest = 0U;
    walks = 0U;
    block = heap_free_list;
    while (block != 0 && walks < heap_max_walks()) {
        if (block->size_bytes > largest) {
            largest = block->size_bytes;
        }
        block = block->next_free;
        walks++;
    }

    return largest;
}

uint32_t heap_error_count(void) {
    return heap_diag_error_count;
}

uint32_t heap_error_flags(void) {
    return heap_diag_error_flags;
}

const char* heap_last_error_message(void) {
    return heap_diag_last_error_message;
}

int heap_run_self_test(void) {
    void* a;
    void* b;
    void* c;
    void* d;
    size_t free_before;
    size_t free_after;

    if (!heap_ready) {
        return 0;
    }

    free_before = heap_bytes_free();
    if (!heap_check_integrity_internal(1)) {
        return 0;
    }

    a = kmalloc(64U);
    b = kmalloc(128U);
    c = kcalloc(16U, 8U);
    if (a == 0 || b == 0 || c == 0) {
        heap_note_error(TERMOB_HEAP_DIAG_SELFTEST,
                        "TERMOB: heap self-test allocation failed");
        if (a != 0) {
            kfree(a);
        }
        if (b != 0) {
            kfree(b);
        }
        if (c != 0) {
            kfree(c);
        }
        return 0;
    }

    kfree(b);
    d = kmalloc(80U);
    if (d == 0) {
        heap_note_error(TERMOB_HEAP_DIAG_SELFTEST,
                        "TERMOB: heap self-test reuse allocation failed");
        kfree(a);
        kfree(c);
        return 0;
    }

    kfree(d);
    kfree(c);
    kfree(a);

    if (!heap_check_integrity_internal(1)) {
        return 0;
    }

    free_after = heap_bytes_free();
    if (free_after != free_before) {
        heap_note_error(TERMOB_HEAP_DIAG_SELFTEST,
                        "TERMOB: heap self-test free space mismatch");
        return 0;
    }

    heap_log_event("TERMOB: heap self-test ok");
    return 1;
}

size_t heap_bytes_total(void) {
    return heap_total_payload_bytes();
}

size_t heap_bytes_used(void) {
    size_t total;
    size_t free_bytes;

    total = heap_bytes_total();
    free_bytes = heap_bytes_free();
    return total >= free_bytes ? (total - free_bytes) : 0U;
}

size_t heap_bytes_free(void) {
    heap_block_t* block;
    size_t free_bytes;
    size_t walks;

    if (!heap_ready) {
        return 0U;
    }

    free_bytes = 0U;
    walks = 0U;
    block = heap_free_list;
    while (block != 0 && walks < heap_max_walks()) {
        free_bytes += block->size_bytes;
        block = block->next_free;
        walks++;
    }

    return free_bytes;
}

uint32_t heap_start_address(void) {
    return (uint32_t)heap_start_ptr;
}

uint32_t heap_end_address(void) {
    return (uint32_t)heap_end_ptr;
}
