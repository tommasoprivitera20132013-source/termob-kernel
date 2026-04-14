#ifndef TERMOB_HEAP_H
#define TERMOB_HEAP_H

#include <stddef.h>
#include <stdint.h>

void heap_init(void);
int heap_is_initialized(void);
void* kmalloc(size_t size);
void* kcalloc(size_t count, size_t size);
void kfree(void* ptr);
int heap_integrity_status(void);
int heap_check_integrity(void);
size_t heap_total_block_count(void);
size_t heap_used_block_count(void);
size_t heap_free_block_count(void);
size_t heap_largest_free_block(void);
uint32_t heap_error_count(void);
uint32_t heap_error_flags(void);
const char* heap_last_error_message(void);
int heap_run_self_test(void);
size_t heap_bytes_total(void);
size_t heap_bytes_used(void);
size_t heap_bytes_free(void);
uint32_t heap_start_address(void);
uint32_t heap_end_address(void);

#endif
