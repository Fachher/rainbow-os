/* Minimal libc bits the compiler/program needs. Freestanding gcc still emits
   calls to memset/memcpy for initializers and struct copies. */
#include "types.h"

void *memset(void *dst, int val, size_t n) {
    uint8_t *p = (uint8_t *)dst;
    while (n--) *p++ = (uint8_t)val;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
    return dst;
}
