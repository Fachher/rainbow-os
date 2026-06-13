#ifndef VGA_H
#define VGA_H

#include "include/types.h"

/* Text grid for the 800x600x8bpp framebuffer console (8x16 glyphs). */
#define VGA_GLYPH_W     8
#define VGA_GLYPH_H     16
#define VGA_WIDTH       100     /* 800 / 8  */
#define VGA_HEIGHT      37      /* 600 / 16 */

enum vga_color {
    VGA_BLACK        = 0,
    VGA_BLUE         = 1,
    VGA_GREEN        = 2,
    VGA_CYAN         = 3,
    VGA_RED          = 4,
    VGA_MAGENTA      = 5,
    VGA_BROWN        = 6,
    VGA_LIGHT_GREY   = 7,
    VGA_DARK_GREY    = 8,
    VGA_LIGHT_BLUE   = 9,
    VGA_LIGHT_GREEN  = 10,
    VGA_LIGHT_CYAN   = 11,
    VGA_LIGHT_RED    = 12,
    VGA_LIGHT_MAGENTA= 13,
    VGA_YELLOW       = 14,
    VGA_WHITE        = 15,
};

void vga_init(void);
void vga_clear(void);
void vga_putchar(char c);
void vga_write(const char *str);
void vga_set_color(uint8_t fg, uint8_t bg);
void vga_write_dec(uint32_t val);
void vga_set_cursor(uint8_t row, uint8_t col);
void vga_putchar_at(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg);
uint8_t vga_get_rows(void);
uint8_t vga_get_cols(void);

/* Reprogram palette indices 0-15 with the standard console colors (call after
   the gfx demo, which overwrites the palette with the rainbow ramp). */
void vga_reset_palette(void);

#endif
