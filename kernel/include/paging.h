#ifndef PAGING_H
#define PAGING_H

#include "types.h"

#define PAGE_PRESENT    0x01
#define PAGE_WRITE      0x02
#define PAGE_USER       0x04

void paging_init(void);

#endif
