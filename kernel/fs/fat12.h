#ifndef FAT12_H
#define FAT12_H

#include "include/types.h"

#define FAT12_FILENAME_LEN  8
#define FAT12_EXT_LEN       3
#define FAT12_MAX_FILES     16

/* Directory entry as returned by fat12_list_root */
struct fat12_dirent {
    char     name[13];      /* "FILENAME.EXT\0" */
    uint32_t size;
    uint8_t  attr;
};

/* Initialize FAT12 driver with a memory-backed volume */
void fat12_init(uint8_t *volume, uint32_t volume_size);

/* List root directory. Returns number of entries written to buf. */
int fat12_list_root(struct fat12_dirent *buf, int max_entries);

/* Read file by name (e.g. "README  TXT" in 8.3 or "readme.txt" in friendly form).
   Returns bytes read, or -1 if not found. */
int fat12_read_file(const char *name, uint8_t *buf, uint32_t buf_size);

/* Write/overwrite file. Returns 0 on success, -1 on error. */
int fat12_write_file(const char *name, const uint8_t *data, uint32_t size);

/* Delete file. Returns 0 on success, -1 if not found. */
int fat12_delete_file(const char *name);

#endif
