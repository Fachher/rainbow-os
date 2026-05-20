#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "include/types.h"

void keyboard_init(void);
void keyboard_wait_any(void);
bool keyboard_has_key(void);
char keyboard_getchar(void);

#endif
