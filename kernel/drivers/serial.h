#ifndef SERIAL_H
#define SERIAL_H

#include "include/types.h"

#define COM1_PORT 0x3F8

void serial_init(void);
void serial_putchar(char c);
void serial_write(const char *str);
void serial_write_hex(uint32_t val);

/* Log macro with file and line info */
#define serial_log(msg) do { \
    serial_write("[" __FILE__ ":"); \
    serial_write_dec(__LINE__); \
    serial_write("] "); \
    serial_write(msg); \
    serial_write("\r\n"); \
} while(0)

void serial_write_dec(uint32_t val);

#endif
