#ifndef PMM_H
#define PMM_H

#include "types.h"

#define PAGE_SIZE       4096
#define TOTAL_MEMORY    (32 * 1024 * 1024)  /* 32 MB */
#define TOTAL_FRAMES    (TOTAL_MEMORY / PAGE_SIZE)

void     pmm_init(uint32_t kernel_end);
uint32_t pmm_alloc_frame(void);
void     pmm_free_frame(uint32_t phys_addr);
uint32_t pmm_free_count(void);
uint32_t pmm_used_count(void);

#endif
