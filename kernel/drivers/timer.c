#include "timer.h"
#include "serial.h"
#include "include/idt.h"
#include "include/pic.h"
#include "include/io.h"

#define PIT_CH0      0x40
#define PIT_CMD      0x43
#define PIT_FREQ     1193182    /* Base PIT input clock (Hz) */

static volatile uint32_t ticks;

static void timer_handler(struct isr_frame *frame) {
    (void)frame;
    ticks++;
}

void timer_init(uint32_t hz) {
    ticks = 0;

    uint32_t divisor = PIT_FREQ / hz;
    if (divisor > 0xFFFF) divisor = 0xFFFF;

    /* Channel 0, lobyte/hibyte, mode 3 (square wave), binary */
    outb(PIT_CMD, 0x36);
    outb(PIT_CH0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CH0, (uint8_t)((divisor >> 8) & 0xFF));

    register_interrupt_handler(32, timer_handler);   /* ISR 32 = IRQ0 */
    pic_irq_unmask(0);

    __asm__ volatile("sti");

    serial_log("PIT timer initialized (IRQ0)");
}

uint32_t timer_ticks(void) {
    return ticks;
}
