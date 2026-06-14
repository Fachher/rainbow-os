/* Kernel-API compatibility shims for userland.
 *
 * The editor (and other ported subsystems) call kernel functions by name
 * (vga_putchar_at, keyboard_getchar, fat12_read_file, ...). Here those names are
 * implemented as thin wrappers over syscalls, so the subsystem source compiles
 * unchanged in ring 3. */

#include "syscall.h"
#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "fs/fat12.h"
#include "fs/diskfs.h"

void vga_clear(void) { sys_clear(); }
void vga_set_cursor(uint8_t row, uint8_t col) { sys_setcur(row, col); }
void vga_putchar_at(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg) {
    sys_putat(row, col, (uint8_t)c, fg, bg);
}
uint8_t vga_get_rows(void) { return (uint8_t)(sys_dims() >> 8); }
uint8_t vga_get_cols(void) { return (uint8_t)(sys_dims() & 0xFF); }
void vga_putchar(char c) { sys_putchar((uint8_t)c); }
void vga_write(const char *s) { sys_puts(s); }
void vga_set_color(uint8_t fg, uint8_t bg) { sys_setcolor(fg, bg); }

int keyboard_getchar(void) { return sys_getchar(); }
bool keyboard_has_key(void) { return (bool)sys_haskey(); }

int fat12_read_file(const char *name, uint8_t *buf, uint32_t buf_size) {
    return sys_readfile(name, buf, buf_size);
}
int diskfs_write_file(const char *name, const uint8_t *data, uint32_t size) {
    return sys_writefile(name, data, size);
}
