/* Framebuffer text console.
 *
 * Despite the historical "vga" name, this renders an 800x600x8bpp graphical
 * console using the Cirrus SVGA driver and the VGA ROM 8x16 font. The public
 * API matches the old text-mode driver so all callers (shell, editor, BASIC,
 * cc, kernel boot messages) are unchanged. The grid is 100x37 cells. */

#include "vga.h"
#include "svga.h"
#include "lib/string.h"

static uint16_t cursor_row = 0;
static uint16_t cursor_col = 0;
static uint8_t  current_color = 0;
static int      cursor_drawn = 0;

/* Shadow grid: one cell = char | (attr << 8), attr = fg | (bg << 4).
   Mirrors the framebuffer so we can dirty-check blits and erase the cursor. */
static uint16_t cells[VGA_HEIGHT * VGA_WIDTH];

/* Standard 16-color console palette (6-bit RGB, matches VGA text colors). */
static const uint8_t console_palette[16][3] = {
    { 0, 0, 0}, { 0, 0,42}, { 0,42, 0}, { 0,42,42},
    {42, 0, 0}, {42, 0,42}, {42,21, 0}, {42,42,42},
    {21,21,21}, {21,21,63}, {21,63,21}, {21,63,63},
    {63,21,21}, {63,21,63}, {63,63,21}, {63,63,63},
};

static inline uint8_t vga_color(uint8_t fg, uint8_t bg) {
    return fg | (bg << 4);
}

static inline uint16_t make_cell(char c) {
    return (uint16_t)(uint8_t)c | ((uint16_t)current_color << 8);
}

void vga_reset_palette(void) {
    for (int i = 0; i < 16; i++)
        svga_set_palette(i, console_palette[i][0],
                            console_palette[i][1],
                            console_palette[i][2]);
}

/* Draw a glyph cell to the framebuffer unconditionally. */
static void render_cell(uint8_t row, uint8_t col, uint16_t cell) {
    uint8_t ch   = cell & 0xFF;
    uint8_t attr = cell >> 8;
    uint8_t fg   = attr & 0x0F;
    uint8_t bg   = (attr >> 4) & 0x0F;
    const uint8_t *glyph = svga_rom_font() + (uint32_t)ch * VGA_GLYPH_H;
    int x0 = col * VGA_GLYPH_W;
    int y0 = row * VGA_GLYPH_H;
    for (int line = 0; line < VGA_GLYPH_H; line++) {
        uint8_t bits = glyph[line];
        int y = y0 + line;
        for (int bit = 0; bit < VGA_GLYPH_W; bit++)
            svga_putpixel(x0 + bit, y, (bits & (0x80 >> bit)) ? fg : bg);
    }
}

/* Blit with dirty-check: skip if the cell already holds this value. */
static void blit_cell(uint8_t row, uint8_t col, uint16_t cell) {
    uint32_t idx = (uint32_t)row * VGA_WIDTH + col;
    if (cells[idx] == cell) return;
    cells[idx] = cell;
    render_cell(row, col, cell);
}

static void draw_cursor(void) {
    uint16_t cell = cells[cursor_row * VGA_WIDTH + cursor_col];
    uint8_t fg = (cell >> 8) & 0x0F;
    if (fg == ((cell >> 12) & 0x0F)) fg = VGA_LIGHT_GREY;   /* avoid invisible */
    int x0 = cursor_col * VGA_GLYPH_W;
    int y0 = cursor_row * VGA_GLYPH_H;
    for (int line = VGA_GLYPH_H - 2; line < VGA_GLYPH_H; line++)
        for (int bit = 0; bit < VGA_GLYPH_W; bit++)
            svga_putpixel(x0 + bit, y0 + line, fg);
    cursor_drawn = 1;
}

static void hide_cursor(void) {
    if (!cursor_drawn) return;
    render_cell(cursor_row, cursor_col, cells[cursor_row * VGA_WIDTH + cursor_col]);
    cursor_drawn = 0;
}

static void vga_scroll(void) {
    if (cursor_row < VGA_HEIGHT) return;

    uint32_t row_bytes = (uint32_t)VGA_GLYPH_H * SVGA_WIDTH;
    uint32_t total     = (uint32_t)SVGA_WIDTH * SVGA_HEIGHT;

    /* Shift the framebuffer up by one text row and shift the shadow grid. */
    svga_copy(0, row_bytes, total - row_bytes);
    memmove(cells, cells + VGA_WIDTH,
            (VGA_HEIGHT - 1) * VGA_WIDTH * sizeof(uint16_t));

    /* Clear the last text row in both the shadow grid and the framebuffer. */
    uint16_t blank = make_cell(' ');
    uint8_t  bg    = (current_color >> 4) & 0x0F;
    for (int i = 0; i < VGA_WIDTH; i++)
        cells[(VGA_HEIGHT - 1) * VGA_WIDTH + i] = blank;
    svga_fill_rect(0, (VGA_HEIGHT - 1) * VGA_GLYPH_H, SVGA_WIDTH, VGA_GLYPH_H, bg);

    cursor_row = VGA_HEIGHT - 1;
}

void vga_init(void) {
    svga_set_mode_gfx();          /* save ROM font + enter 800x600x8bpp */
    vga_reset_palette();
    current_color = vga_color(VGA_LIGHT_GREEN, VGA_BLACK);
    cursor_drawn = 0;
    vga_clear();
}

void vga_clear(void) {
    uint8_t bg = (current_color >> 4) & 0x0F;
    svga_clear(bg);
    uint16_t blank = make_cell(' ');
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        cells[i] = blank;
    cursor_row = 0;
    cursor_col = 0;
    cursor_drawn = 0;
    draw_cursor();
}

void vga_set_color(uint8_t fg, uint8_t bg) {
    current_color = vga_color(fg, bg);
}

void vga_putchar(char c) {
    hide_cursor();

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
        blit_cell(cursor_row, cursor_col, make_cell(' '));
    } else if (c == '\t') {
        cursor_col = (cursor_col + 8) & ~7;
    } else {
        blit_cell(cursor_row, cursor_col, make_cell(c));
        cursor_col++;
    }

    if (cursor_col >= VGA_WIDTH) {
        cursor_col = 0;
        cursor_row++;
    }

    vga_scroll();
    draw_cursor();
}

void vga_write(const char *str) {
    while (*str)
        vga_putchar(*str++);
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
    while (i > 0)
        vga_putchar(buf[--i]);
}

void vga_set_cursor(uint8_t row, uint8_t col) {
    if (row >= VGA_HEIGHT) row = VGA_HEIGHT - 1;
    if (col >= VGA_WIDTH) col = VGA_WIDTH - 1;
    hide_cursor();
    cursor_row = row;
    cursor_col = col;
    draw_cursor();
}

void vga_putchar_at(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg) {
    if (row >= VGA_HEIGHT || col >= VGA_WIDTH) return;
    uint16_t cell = (uint16_t)(uint8_t)c | ((uint16_t)vga_color(fg, bg) << 8);
    blit_cell(row, col, cell);
}

uint8_t vga_get_rows(void) {
    return VGA_HEIGHT;
}

uint8_t vga_get_cols(void) {
    return VGA_WIDTH;
}
