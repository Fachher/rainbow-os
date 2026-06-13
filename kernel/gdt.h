#ifndef GDT_H
#define GDT_H

#include "include/types.h"

/* Segment selectors (index << 3 | RPL) */
#define SEL_KCODE 0x08
#define SEL_KDATA 0x10
#define SEL_UCODE 0x1B   /* user code, RPL 3 */
#define SEL_UDATA 0x23   /* user data, RPL 3 */
#define SEL_TSS   0x28

/* Set up the kernel GDT (kernel + user segments + TSS) and load it. */
void gdt_init(void);

/* Set the ring-0 stack the CPU switches to on a ring 3->0 transition. */
void tss_set_esp0(uint32_t esp0);

#endif
