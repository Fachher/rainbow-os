#include "diskfs.h"
#include "fat12.h"
#include "ramdisk.h"
#include "drivers/ata.h"
#include "drivers/serial.h"
#include "drivers/vga.h"
#include "lib/string.h"

static uint8_t diskfs_volume[DISKFS_SIZE];
static bool disk_available = false;

/* Read 16-bit LE value from buffer */
static uint16_t read16(const uint8_t *p) {
    return p[0] | ((uint16_t)p[1] << 8);
}

/* Check if the volume has a valid FAT12 BPB */
static bool bpb_valid(const uint8_t *vol) {
    /* Check jump instruction (EB xx 90 or E9 xx xx) */
    if (vol[0] != 0xEB && vol[0] != 0xE9) return false;
    /* Check bytes per sector == 512 */
    if (read16(vol + 11) != 512) return false;
    return true;
}

/* Format a fresh FAT12 volume in the buffer */
static void format_volume(void) {
    memset(diskfs_volume, 0, DISKFS_SIZE);

    uint8_t *bpb = diskfs_volume;

    /* Jump + NOP */
    bpb[0] = 0xEB; bpb[1] = 0x3C; bpb[2] = 0x90;

    /* OEM name */
    memcpy(bpb + 3, "RNBWOS  ", 8);

    /* BPB fields */
    bpb[11] = 0x00; bpb[12] = 0x02;  /* bytes/sector = 512 */
    bpb[13] = 1;                       /* sectors/cluster */
    bpb[14] = 1; bpb[15] = 0;         /* reserved sectors */
    bpb[16] = 1;                       /* number of FATs */
    bpb[17] = 64; bpb[18] = 0;        /* root dir entries */
    bpb[19] = 0x00; bpb[20] = 0x08;   /* total sectors = 2048 */
    bpb[21] = 0xF8;                    /* media byte (hard disk) */
    bpb[22] = 3; bpb[23] = 0;         /* sectors per FAT */
    bpb[24] = 63; bpb[25] = 0;        /* sectors per track */
    bpb[26] = 16; bpb[27] = 0;        /* number of heads */

    /* FAT: reserved entries 0 and 1 */
    uint32_t fat_offset = 512;  /* 1 reserved sector */
    diskfs_volume[fat_offset + 0] = 0xF8;
    diskfs_volume[fat_offset + 1] = 0xFF;
    diskfs_volume[fat_offset + 2] = 0xFF;

    serial_log("diskfs: formatted fresh FAT12 volume");
}

/* Read the entire partition from HDD in chunks of 255 sectors (max for ATA PIO) */
static int read_partition(void) {
    uint32_t sectors_remaining = HDD_FS_SECTOR_COUNT;
    uint32_t lba = HDD_FS_START_SECTOR;
    uint8_t *buf = diskfs_volume;

    while (sectors_remaining > 0) {
        uint8_t chunk = (sectors_remaining > 255) ? 255 : (uint8_t)sectors_remaining;
        if (ata_read_sectors(lba, chunk, buf) != 0)
            return -1;
        lba += chunk;
        buf += chunk * 512;
        sectors_remaining -= chunk;
    }
    return 0;
}

/* Write the entire partition back to HDD */
static int write_partition(void) {
    uint32_t sectors_remaining = HDD_FS_SECTOR_COUNT;
    uint32_t lba = HDD_FS_START_SECTOR;
    const uint8_t *buf = diskfs_volume;

    while (sectors_remaining > 0) {
        uint8_t chunk = (sectors_remaining > 255) ? 255 : (uint8_t)sectors_remaining;
        if (ata_write_sectors(lba, chunk, buf) != 0)
            return -1;
        lba += chunk;
        buf += chunk * 512;
        sectors_remaining -= chunk;
    }
    return 0;
}

void diskfs_init(void) {
    /* Try ATA initialization */
    if (ata_init() != 0) {
        /* No HDD — fall back to ramdisk */
        ramdisk_init();
        vga_write("[OK] FAT12 ramdisk ready (no disk)\n");
        return;
    }

    /* Read partition from HDD */
    if (read_partition() != 0) {
        serial_log("diskfs: failed to read partition, falling back to ramdisk");
        ramdisk_init();
        vga_write("[OK] FAT12 ramdisk ready (disk read failed)\n");
        return;
    }

    disk_available = true;

    /* Validate BPB */
    if (!bpb_valid(diskfs_volume)) {
        serial_log("diskfs: no valid FAT12 found, formatting");
        format_volume();
        write_partition();
    }

    /* Initialize FAT12 driver with our buffer */
    fat12_init(diskfs_volume, DISKFS_SIZE);

    vga_write("[OK] Persistent storage ready (ATA, 1 MB FAT12)\n");
    serial_log("diskfs: persistent storage initialized");
}

int diskfs_sync(void) {
    if (!disk_available) return -1;
    return write_partition();
}

int diskfs_write_file(const char *name, const uint8_t *data, uint32_t size) {
    int result = fat12_write_file(name, data, size);
    if (result == 0 && disk_available)
        diskfs_sync();
    return result;
}

int diskfs_delete_file(const char *name) {
    int result = fat12_delete_file(name);
    if (result == 0 && disk_available)
        diskfs_sync();
    return result;
}
