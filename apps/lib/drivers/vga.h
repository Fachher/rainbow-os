#ifndef USER_VGA_H
#define USER_VGA_H

#include "include/types.h"

/* Console colors (palette indices 0-15), matching the kernel enum. */
enum vga_color {
    VGA_BLACK = 0, VGA_BLUE, VGA_GREEN, VGA_CYAN, VGA_RED, VGA_MAGENTA,
    VGA_BROWN, VGA_LIGHT_GREY, VGA_DARK_GREY, VGA_LIGHT_BLUE, VGA_LIGHT_GREEN,
    VGA_LIGHT_CYAN, VGA_LIGHT_RED, VGA_LIGHT_MAGENTA, VGA_YELLOW, VGA_WHITE,
};

/* Console text output (kernel-named, backed by syscalls). */
void    vga_clear(void);
void    vga_set_cursor(uint8_t row, uint8_t col);
void    vga_putchar_at(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg);
uint8_t vga_get_rows(void);
uint8_t vga_get_cols(void);
void    vga_putchar(char c);
void    vga_write(const char *s);
void    vga_set_color(uint8_t fg, uint8_t bg);

#endif
