#ifndef STRING_H
#define STRING_H

#include "include/types.h"

void *memset(void *dest, int val, size_t count);
void *memcpy(void *dest, const void *src, size_t count);
size_t strlen(const char *str);
int strcmp(const char *s1, const char *s2);

#endif
