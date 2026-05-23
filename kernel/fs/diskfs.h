#ifndef DISKFS_H
#define DISKFS_H

#include "include/types.h"

#define HDD_FS_START_SECTOR  262
#define HDD_FS_SECTOR_COUNT  2048
#define DISKFS_SIZE          (HDD_FS_SECTOR_COUNT * 512)  /* 1 MB */

/* Initialize persistent filesystem from HDD, fallback to ramdisk */
void diskfs_init(void);

/* Flush filesystem buffer to HDD */
int diskfs_sync(void);

/* Write file and auto-sync to disk */
int diskfs_write_file(const char *name, const uint8_t *data, uint32_t size);

/* Delete file and auto-sync to disk */
int diskfs_delete_file(const char *name);

#endif
