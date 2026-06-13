#ifndef SVGA_H
#define SVGA_H

#include "include/types.h"

#define SVGA_WIDTH  800
#define SVGA_HEIGHT 600

void svga_init(void);
void svga_set_mode_gfx(void);
void svga_set_mode_text(void);

void svga_clear(uint8_t color);
void svga_putpixel(int x, int y, uint8_t color);
void svga_hline(int x, int y, int w, uint8_t color);
void svga_vline(int x, int y, int h, uint8_t color);
void svga_fill_rect(int x, int y, int w, int h, uint8_t color);
void svga_set_palette(uint8_t index, uint8_t r, uint8_t g, uint8_t b);

/* Full 256-color rainbow palette (used by the gfx demo). */
void svga_rainbow_palette(void);

/* Pointer to the saved VGA ROM font (256 glyphs x 16 bytes), populated when
   graphics mode is entered. Used by the framebuffer text console. */
const uint8_t *svga_rom_font(void);

/* Copy a linear range within the banked framebuffer (handles 64 KB banks). */
void svga_copy(uint32_t dst_off, uint32_t src_off, uint32_t count);

/* Blit a full-screen RAM back buffer to the framebuffer in one pass. */
void svga_blit(const uint8_t *src);

#endif
