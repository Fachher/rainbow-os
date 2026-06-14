#ifndef USER_KEYBOARD_H
#define USER_KEYBOARD_H

#include "include/types.h"

/* Special key codes (match the kernel keyboard driver). */
#define KEY_UP      0x80
#define KEY_DOWN    0x81
#define KEY_LEFT    0x82
#define KEY_RIGHT   0x83
#define KEY_HOME    0x84
#define KEY_END     0x85
#define KEY_PGUP    0x86
#define KEY_PGDN    0x87
#define KEY_DELETE  0x88
#define KEY_ESCAPE  0x1B
#define KEY_CTRL(c) ((c) & 0x1F)

int  keyboard_getchar(void);  /* blocking, backed by SYS_GETCHAR */
bool keyboard_has_key(void);  /* SYS_HASKEY */

#endif
