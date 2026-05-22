#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "include/types.h"

/* Special key codes (> 127 to avoid ASCII collision) */
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

/* Ctrl+letter: keyboard_getchar returns KEY_CTRL('Q') == 0x11, etc. */
#define KEY_CTRL(c) ((c) & 0x1F)

void keyboard_init(void);
void keyboard_wait_any(void);
bool keyboard_has_key(void);
int  keyboard_getchar(void);

#endif
