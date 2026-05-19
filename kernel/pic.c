#include "include/pic.h"
#include "include/io.h"
#include "drivers/serial.h"

void pic_init(void) {
    /* ICW1: Begin initialization (cascade mode, ICW4 needed) */
    outb(PIC1_CMD,  0x11);
    io_wait();
    outb(PIC2_CMD,  0x11);
    io_wait();

    /* ICW2: Vector offsets - remap IRQ 0-7 to ISR 32-39, IRQ 8-15 to ISR 40-47 */
    outb(PIC1_DATA, IRQ_BASE);          /* Master: IRQ 0-7 -> ISR 32-39 */
    io_wait();
    outb(PIC2_DATA, IRQ_BASE + 8);     /* Slave:  IRQ 8-15 -> ISR 40-47 */
    io_wait();

    /* ICW3: Cascade wiring */
    outb(PIC1_DATA, 0x04);             /* Master: slave on IRQ2 */
    io_wait();
    outb(PIC2_DATA, 0x02);             /* Slave: cascade identity 2 */
    io_wait();

    /* ICW4: 8086 mode */
    outb(PIC1_DATA, 0x01);
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();

    /* Mask all IRQs initially (except cascade IRQ2) */
    outb(PIC1_DATA, 0xFB);             /* 1111 1011 - only IRQ2 unmasked */
    outb(PIC2_DATA, 0xFF);             /* All masked */

    serial_log("PIC remapped (IRQ 0-15 -> ISR 32-47)");
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_CMD, 0x20);          /* EOI to slave */
    }
    outb(PIC1_CMD, 0x20);              /* EOI to master */
}

void pic_irq_unmask(uint8_t irq) {
    uint16_t port;
    uint8_t val;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    val = inb(port) & ~(1 << irq);
    outb(port, val);
}

void pic_irq_mask(uint8_t irq) {
    uint16_t port;
    uint8_t val;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    val = inb(port) | (1 << irq);
    outb(port, val);
}
