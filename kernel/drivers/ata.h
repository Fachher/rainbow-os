#ifndef ATA_H
#define ATA_H

#include "include/types.h"

/* Initialize ATA driver (identify primary master drive) */
int ata_init(void);

/* Read sectors using 28-bit LBA PIO */
int ata_read_sectors(uint32_t lba, uint8_t count, uint8_t *buf);

/* Write sectors using 28-bit LBA PIO */
int ata_write_sectors(uint32_t lba, uint8_t count, const uint8_t *buf);

#endif
