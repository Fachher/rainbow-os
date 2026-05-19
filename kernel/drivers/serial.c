#include "serial.h"
#include "include/io.h"

void serial_init(void) {
    outb(COM1_PORT + 1, 0x00);    /* Disable interrupts */
    outb(COM1_PORT + 3, 0x80);    /* Enable DLAB (set baud rate divisor) */
    outb(COM1_PORT + 0, 0x03);    /* Divisor low byte: 38400 baud */
    outb(COM1_PORT + 1, 0x00);    /* Divisor high byte */
    outb(COM1_PORT + 3, 0x03);    /* 8 bits, no parity, 1 stop bit */
    outb(COM1_PORT + 2, 0xC7);    /* Enable FIFO, clear, 14-byte threshold */
    outb(COM1_PORT + 4, 0x0B);    /* IRQs enabled, RTS/DSR set */
}

static int serial_transmit_ready(void) {
    return inb(COM1_PORT + 5) & 0x20;
}

void serial_putchar(char c) {
    while (!serial_transmit_ready());
    outb(COM1_PORT, c);
}

void serial_write(const char *str) {
    while (*str) {
        if (*str == '\n') {
            serial_putchar('\r');
        }
        serial_putchar(*str++);
    }
}

void serial_write_hex(uint32_t val) {
    const char hex[] = "0123456789ABCDEF";
    serial_write("0x");
    for (int i = 28; i >= 0; i -= 4) {
        serial_putchar(hex[(val >> i) & 0xF]);
    }
}

void serial_write_dec(uint32_t val) {
    char buf[12];
    int i = 0;

    if (val == 0) {
        serial_putchar('0');
        return;
    }

    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }

    while (--i >= 0) {
        serial_putchar(buf[i]);
    }
}
