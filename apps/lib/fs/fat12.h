#ifndef USER_FAT12_H
#define USER_FAT12_H

#include "include/types.h"

/* Read a file (SYS_READFILE). Returns bytes read, or -1 if not found. */
int fat12_read_file(const char *name, uint8_t *buf, uint32_t buf_size);

#endif
