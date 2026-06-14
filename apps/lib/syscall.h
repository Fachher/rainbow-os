#ifndef USER_SYSCALL_H
#define USER_SYSCALL_H

#include "types.h"

/* Userland wrappers around int 0x80. See kernel/cc/runtime.c for the kernel
   side. Each is a thin stub in syscall.asm. */
void     sys_putchar(int c);
int      sys_getchar(void);
void     sys_puts(const char *s);
void     sys_exit(int code);
int      sys_printf(const char *fmt, ...);
uint32_t sys_ticks(void);
int      sys_keydown(int scancode);
int      sys_keydown_ext(int scancode);
void     sys_blit(const uint8_t *buf);     /* SVGA_WIDTH*SVGA_HEIGHT bytes -> screen */
void     sys_getfont(uint8_t *dst);        /* copies 4096-byte VGA ROM font */
void     sys_yield(void);                  /* idle until the next interrupt */
void     sys_clear(void);                  /* clear/repaint the text console */
void     sys_kbflush(void);

/* tiny libc (umem.c) */
void *memset(void *dst, int val, size_t n);
void *memcpy(void *dst, const void *src, size_t n);

#endif
