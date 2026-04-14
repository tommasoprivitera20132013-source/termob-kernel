#ifndef TERMOB_PAGING_H
#define TERMOB_PAGING_H

#include <stdint.h>

void paging_init(void);
int paging_is_enabled(void);
uint32_t paging_directory_physical_address(void);
uint32_t paging_identity_mapped_bytes(void);
uint32_t paging_table_count(void);
uint32_t paging_page_size_bytes(void);

#endif
