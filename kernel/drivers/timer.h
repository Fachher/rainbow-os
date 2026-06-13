#ifndef TIMER_H
#define TIMER_H

#include "include/types.h"

/* Program the 8254 PIT channel 0 to fire IRQ0 at hz Hz and start counting.
   Also enables interrupts (sti). */
void timer_init(uint32_t hz);

/* Monotonic tick counter (increments hz times per second). */
uint32_t timer_ticks(void);

#endif
