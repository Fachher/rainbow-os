#ifndef PAGING_H
#define PAGING_H

#include "types.h"

#define PAGE_PRESENT    0x01
#define PAGE_WRITE      0x02
#define PAGE_USER       0x04

/* Ring-3 program window: code/data load at the base, user stack grows down
   from the top. Only this range is marked user-accessible; everything else
   (kernel, framebuffer, page tables) stays supervisor-only. */
#define USER_REGION_BASE 0x200000   /* = CG_LOAD_ADDR */
#define USER_REGION_TOP  0x300000   /* 1 MB window */
#define USER_STACK_TOP   0x300000

void paging_init(void);

#endif
