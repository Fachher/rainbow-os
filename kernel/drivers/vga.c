#include "vga.h"
#include "include/io.h"
#include "lib/string.h"

static volatile uint16_t *vga_buffer = (volatile uint16_t *)VGA_BUFFER_ADDR;
static uint16_t cursor_row = 0;
static uint16_t cursor_col = 0;
static uint8_t  current_color = 0;

static inline uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

static inline uint8_t vga_color(uint8_t fg, uint8_t bg) {
    return fg | (bg << 4);
}

static void vga_update_cursor(void) {
    uint16_t pos = cursor_row * VGA_WIDTH + cursor_col;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void vga_scroll(void) {
    if (cursor_row < VGA_HEIGHT) return;

    /* Move all rows up by one */
    for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++) {
        vga_buffer[i] = vga_buffer[i + VGA_WIDTH];
    }

    /* Clear last row */
    uint16_t blank = vga_entry(' ', current_color);
    for (int i = 0; i < VGA_WIDTH; i++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + i] = blank;
    }

    cursor_row = VGA_HEIGHT - 1;
}

void vga_init(void) {
    current_color = vga_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_clear();
}

void vga_clear(void) {
    uint16_t blank = vga_entry(' ', current_color);
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = blank;
    }
    cursor_row = 0;
    cursor_col = 0;
    vga_update_cursor();
}

void vga_set_color(uint8_t fg, uint8_t bg) {
    current_color = vga_color(fg, bg);
}

void vga_putchar(char c) {
    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
    } else if (c == '\r') {
        cursor_col = 0;
    } else if (c == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
        } else if (cursor_row > 0) {
            cursor_row--;
            cursor_col = VGA_WIDTH - 1;
        }
        vga_buffer[cursor_row * VGA_WIDTH + cursor_col] = vga_entry(' ', current_color);
    } else if (c == '\t') {
        cursor_col = (cursor_col + 8) & ~7;
    } else {
        vga_buffer[cursor_row * VGA_WIDTH + cursor_col] = vga_entry(c, current_color);
        cursor_col++;
    }

    if (cursor_col >= VGA_WIDTH) {
        cursor_col = 0;
        cursor_row++;
    }

    vga_scroll();
    vga_update_cursor();
}

void vga_write(const char *str) {
    while (*str) {
        vga_putchar(*str++);
    }
}

void vga_write_dec(uint32_t val) {
    if (val == 0) {
        vga_putchar('0');
        return;
    }
    char buf[10];
    int i = 0;
    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    while (i > 0) {
        vga_putchar(buf[--i]);
    }
}

void vga_set_cursor(uint8_t row, uint8_t col) {
    if (row >= VGA_HEIGHT) row = VGA_HEIGHT - 1;
    if (col >= VGA_WIDTH) col = VGA_WIDTH - 1;
    cursor_row = row;
    cursor_col = col;
    vga_update_cursor();
}

void vga_putchar_at(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg) {
    if (row >= VGA_HEIGHT || col >= VGA_WIDTH) return;
    uint8_t color = vga_color(fg, bg);
    vga_buffer[row * VGA_WIDTH + col] = vga_entry(c, color);
}

uint8_t vga_get_rows(void) {
    return VGA_HEIGHT;
}

uint8_t vga_get_cols(void) {
    return VGA_WIDTH;
}
