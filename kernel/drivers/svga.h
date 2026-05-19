#ifndef SVGA_H
#define SVGA_H

#include "include/types.h"

#define SVGA_WIDTH  640
#define SVGA_HEIGHT 480

void svga_init(void);
void svga_set_mode_gfx(void);
void svga_set_mode_text(void);

void svga_clear(uint8_t color);
void svga_putpixel(int x, int y, uint8_t color);
void svga_hline(int x, int y, int w, uint8_t color);
void svga_vline(int x, int y, int h, uint8_t color);
void svga_fill_rect(int x, int y, int w, int h, uint8_t color);
void svga_set_palette(uint8_t index, uint8_t r, uint8_t g, uint8_t b);

#endif
