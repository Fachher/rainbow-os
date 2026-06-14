#ifndef USER_DISKFS_H
#define USER_DISKFS_H

#include "include/types.h"

/* Write/overwrite a file (SYS_WRITEFILE). Returns 0 on success. */
int diskfs_write_file(const char *name, const uint8_t *data, uint32_t size);

#endif
