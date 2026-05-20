#include "svga.h"
#include "serial.h"
#include "include/io.h"
#include "lib/string.h"

#define VGA_FB      0xA0000
#define VGA_FB_SIZE 0x10000     /* 64 KB window */

/* Current bank cached to avoid redundant switches */
static uint8_t current_bank = 0xFF;

/* Saved VGA font from plane 2 (256 chars * 32 bytes each) */
static uint8_t saved_font[8192];

/* ============================================================================
 * VGA register helpers
 * ============================================================================ */

static void vga_write_seq(uint8_t index, uint8_t val) {
    outb(0x3C4, index);
    outb(0x3C5, val);
}

static void vga_write_crtc(uint8_t index, uint8_t val) {
    outb(0x3D4, index);
    outb(0x3D5, val);
}

static void vga_write_gc(uint8_t index, uint8_t val) {
    outb(0x3CE, index);
    outb(0x3CF, val);
}

static void vga_write_ac(uint8_t index, uint8_t val) {
    inb(0x3DA);             /* Reset AC flip-flop */
    outb(0x3C0, index);
    outb(0x3C0, val);
}

static uint8_t vga_read_crtc(uint8_t index) {
    outb(0x3D4, index);
    return inb(0x3D5);
}

/* ============================================================================
 * Cirrus bank switching: GR9 sets read/write bank in 4KB units
 * For 64KB aligned banks: bank_number * 16
 * ============================================================================ */

static void svga_set_bank(uint8_t bank) {
    if (bank == current_bank) return;
    current_bank = bank;
    vga_write_gc(0x09, bank << 4);
}

/* ============================================================================
 * 640x480x8bpp mode setup via standard VGA + Cirrus extensions
 * ============================================================================ */

/* Misc output register */
#define MISC_640x480 0xE3

/* Sequencer registers (index 0-4) */
static const uint8_t seq_640x480[] = {
    0x03,   /* SR0: Reset */
    0x01,   /* SR1: Clocking Mode (8-dot chars) */
    0x0F,   /* SR2: Map Mask (all planes) */
    0x00,   /* SR3: Character Map Select */
    0x0E,   /* SR4: Memory Mode (chain-4, extended memory) */
};

/* CRTC registers (index 0-24) for 640x480 timing, 8bpp */
static const uint8_t crtc_640x480[] = {
    0x5F,   /* CR00: Horizontal Total */
    0x4F,   /* CR01: Horizontal Display End */
    0x50,   /* CR02: Start Horizontal Blanking */
    0x82,   /* CR03: End Horizontal Blanking */
    0x54,   /* CR04: Start Horizontal Retrace */
    0x80,   /* CR05: End Horizontal Retrace */
    0x0B,   /* CR06: Vertical Total */
    0x3E,   /* CR07: Overflow */
    0x00,   /* CR08: Preset Row Scan */
    0x40,   /* CR09: Max Scan Line */
    0x00,   /* CR0A: Cursor Start */
    0x00,   /* CR0B: Cursor End */
    0x00,   /* CR0C: Start Address High */
    0x00,   /* CR0D: Start Address Low */
    0x00,   /* CR0E: Cursor Location High */
    0x00,   /* CR0F: Cursor Location Low */
    0xEA,   /* CR10: Vertical Retrace Start */
    0x0C,   /* CR11: Vertical Retrace End */
    0xDF,   /* CR12: Vertical Display End */
    0x50,   /* CR13: Offset (640/8 = 80 = 0x50 in dword mode) */
    0x40,   /* CR14: Underline Location (dword mode) */
    0xE7,   /* CR15: Start Vertical Blanking */
    0x04,   /* CR16: End Vertical Blanking */
    0xE3,   /* CR17: CRTC Mode Control */
    0xFF,   /* CR18: Line Compare */
};

/* Graphics Controller registers (index 0-8) */
static const uint8_t gc_640x480[] = {
    0x00,   /* GR0: Set/Reset */
    0x00,   /* GR1: Enable Set/Reset */
    0x00,   /* GR2: Color Compare */
    0x00,   /* GR3: Data Rotate */
    0x00,   /* GR4: Read Map Select */
    0x40,   /* GR5: Graphics Mode (256-color shift) */
    0x05,   /* GR6: Miscellaneous (A0000h, 64KB) */
    0x0F,   /* GR7: Color Don't Care */
    0xFF,   /* GR8: Bit Mask */
};

/* Attribute Controller registers (index 0-20) */
static const uint8_t ac_640x480[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x41,   /* AC10: Attribute Mode Control (graphics, 8-bit) */
    0x00,   /* AC11: Overscan Color */
    0x0F,   /* AC12: Color Plane Enable */
    0x00,   /* AC13: Horizontal Pixel Panning */
    0x00,   /* AC14: Color Select */
};

/* ============================================================================
 * 80x25 text mode register values
 * ============================================================================ */

#define MISC_TEXT 0x67

static const uint8_t seq_text[] = {
    0x03, 0x00, 0x03, 0x00, 0x02,
};

static const uint8_t crtc_text[] = {
    0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
    0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x50,
    0x9C, 0x0E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3, 0xFF,
};

static const uint8_t gc_text[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF,
};

static const uint8_t ac_text[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
    0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
    0x0C, 0x00, 0x0F, 0x08, 0x00,
};

/* ============================================================================
 * Mode switching
 * ============================================================================ */

static void program_regs(uint8_t misc,
                         const uint8_t *seq, int seq_count,
                         const uint8_t *crtc, int crtc_count,
                         const uint8_t *gc, int gc_count,
                         const uint8_t *ac, int ac_count) {
    /* Misc output */
    outb(0x3C2, misc);

    /* Sequencer */
    for (int i = 0; i < seq_count; i++)
        vga_write_seq(i, seq[i]);

    /* Unlock CRTC registers */
    vga_write_crtc(0x11, vga_read_crtc(0x11) & 0x7F);

    /* CRTC */
    for (int i = 0; i < crtc_count; i++)
        vga_write_crtc(i, crtc[i]);

    /* Graphics Controller */
    for (int i = 0; i < gc_count; i++)
        vga_write_gc(i, gc[i]);

    /* Attribute Controller */
    inb(0x3DA);     /* Reset flip-flop */
    for (int i = 0; i < ac_count; i++)
        vga_write_ac(i, ac[i]);

    /* Enable display */
    inb(0x3DA);
    outb(0x3C0, 0x20);
}

/* Save VGA font from plane 2 before entering graphics mode */
static void save_font(void) {
    /* Access plane 2 for reading */
    vga_write_seq(0x02, 0x04);   /* SR2: write plane 2 */
    vga_write_seq(0x04, 0x06);   /* SR4: disable chain-4, disable odd/even */
    vga_write_gc(0x04, 0x02);    /* GR4: read plane 2 */
    vga_write_gc(0x05, 0x00);    /* GR5: no shift interleave */
    vga_write_gc(0x06, 0x04);    /* GR6: map at A0000, 64K */

    uint8_t *font_mem = (uint8_t *)0xA0000;
    memcpy(saved_font, font_mem, sizeof(saved_font));

    /* Restore text mode sequencer/GC settings */
    vga_write_seq(0x02, 0x03);
    vga_write_seq(0x04, 0x02);
    vga_write_gc(0x04, 0x00);
    vga_write_gc(0x05, 0x10);
    vga_write_gc(0x06, 0x0E);
}

/* Restore VGA font to plane 2 after returning to text mode */
static void restore_font(void) {
    /* Access plane 2 for writing */
    vga_write_seq(0x02, 0x04);   /* SR2: write plane 2 */
    vga_write_seq(0x04, 0x06);   /* SR4: disable chain-4, disable odd/even */
    vga_write_gc(0x04, 0x02);    /* GR4: read plane 2 */
    vga_write_gc(0x05, 0x00);    /* GR5: no shift interleave */
    vga_write_gc(0x06, 0x04);    /* GR6: map at A0000, 64K */

    uint8_t *font_mem = (uint8_t *)0xA0000;
    memcpy(font_mem, saved_font, sizeof(saved_font));

    /* Restore text mode sequencer/GC settings */
    vga_write_seq(0x02, 0x03);
    vga_write_seq(0x04, 0x02);
    vga_write_gc(0x04, 0x00);
    vga_write_gc(0x05, 0x10);
    vga_write_gc(0x06, 0x0E);
}

void svga_set_mode_gfx(void) {
    save_font();
    program_regs(MISC_640x480,
                 seq_640x480,  5,
                 crtc_640x480, 25,
                 gc_640x480,   9,
                 ac_640x480,   21);

    /* Cirrus extension: SR7 = 0x01 (8bpp, extensions enabled) */
    vga_write_seq(0x07, 0x01);

    current_bank = 0xFF;
    svga_set_bank(0);

    serial_log("SVGA mode: 640x480x8bpp");
}

void svga_set_mode_text(void) {
    /* Reset Cirrus extensions */
    vga_write_seq(0x07, 0x00);

    program_regs(MISC_TEXT,
                 seq_text,  5,
                 crtc_text, 25,
                 gc_text,   9,
                 ac_text,   21);

    /* Restore standard text mode palette (first 16 colors) */
    static const uint8_t text_palette[][3] = {
        { 0, 0, 0}, { 0, 0,42}, { 0,42, 0}, { 0,42,42},
        {42, 0, 0}, {42, 0,42}, {42,21, 0}, {42,42,42},
        {21,21,21}, {21,21,63}, {21,63,21}, {21,63,63},
        {63,21,21}, {63,21,63}, {63,63,21}, {63,63,63},
    };
    outb(0x3C8, 0);
    for (int i = 0; i < 16; i++) {
        outb(0x3C9, text_palette[i][0]);
        outb(0x3C9, text_palette[i][1]);
        outb(0x3C9, text_palette[i][2]);
    }

    restore_font();

    serial_log("Text mode: 80x25");
}

/* ============================================================================
 * Drawing
 * ============================================================================ */

void svga_set_palette(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
    outb(0x3C8, index);
    outb(0x3C9, r);    /* 6-bit values (0-63) */
    outb(0x3C9, g);
    outb(0x3C9, b);
}

void svga_clear(uint8_t color) {
    uint32_t total = SVGA_WIDTH * SVGA_HEIGHT;
    uint8_t *fb = (uint8_t *)VGA_FB;

    for (uint32_t offset = 0; offset < total; offset += VGA_FB_SIZE) {
        uint8_t bank = offset >> 16;
        svga_set_bank(bank);
        uint32_t chunk = total - offset;
        if (chunk > VGA_FB_SIZE) chunk = VGA_FB_SIZE;
        memset(fb, color, chunk);
    }
}

void svga_putpixel(int x, int y, uint8_t color) {
    if (x < 0 || x >= SVGA_WIDTH || y < 0 || y >= SVGA_HEIGHT) return;
    uint32_t offset = y * SVGA_WIDTH + x;
    svga_set_bank(offset >> 16);
    *((uint8_t *)(VGA_FB + (offset & 0xFFFF))) = color;
}

void svga_hline(int x, int y, int w, uint8_t color) {
    for (int i = 0; i < w; i++)
        svga_putpixel(x + i, y, color);
}

void svga_vline(int x, int y, int h, uint8_t color) {
    for (int i = 0; i < h; i++)
        svga_putpixel(x, y + i, color);
}

void svga_fill_rect(int x, int y, int w, int h, uint8_t color) {
    for (int row = 0; row < h; row++)
        svga_hline(x, y + row, w, color);
}

/* ============================================================================
 * Rainbow palette setup (indices 0-255)
 * ============================================================================ */

static void setup_rainbow_palette(void) {
    /* 0 = black */
    svga_set_palette(0, 0, 0, 0);

    /* 1-31: red to yellow */
    for (int i = 0; i < 31; i++)
        svga_set_palette(1 + i, 63, i * 2, 0);

    /* 32-63: yellow to green */
    for (int i = 0; i < 32; i++)
        svga_set_palette(32 + i, 63 - i * 2, 63, 0);

    /* 64-95: green to cyan */
    for (int i = 0; i < 32; i++)
        svga_set_palette(64 + i, 0, 63, i * 2);

    /* 96-127: cyan to blue */
    for (int i = 0; i < 32; i++)
        svga_set_palette(96 + i, 0, 63 - i * 2, 63);

    /* 128-159: blue to magenta */
    for (int i = 0; i < 32; i++)
        svga_set_palette(128 + i, i * 2, 0, 63);

    /* 160-191: magenta to red */
    for (int i = 0; i < 32; i++)
        svga_set_palette(160 + i, 63, 0, 63 - i * 2);

    /* 192-254: grayscale ramp */
    for (int i = 0; i < 63; i++) {
        uint8_t v = i;
        svga_set_palette(192 + i, v, v, v);
    }

    /* 255 = white */
    svga_set_palette(255, 63, 63, 63);
}

void svga_init(void) {
    setup_rainbow_palette();
    serial_log("SVGA palette initialized");
}
