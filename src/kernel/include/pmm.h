#ifndef TERMOB_PMM_H
#define TERMOB_PMM_H

#include <stddef.h>
#include <stdint.h>

void pmm_init(void);
int pmm_is_initialized(void);
uint32_t pmm_alloc_frame(void);
void pmm_free_frame(uint32_t physical_address);
uint32_t pmm_frame_size(void);
uint32_t pmm_total_frames(void);
uint32_t pmm_free_frames(void);
uint32_t pmm_used_frames(void);
uint32_t pmm_total_bytes(void);
uint32_t pmm_free_bytes(void);
uint32_t pmm_used_bytes(void);
uint32_t pmm_bitmap_bytes(void);

#endif
