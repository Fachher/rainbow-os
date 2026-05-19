#include "ramdisk.h"
#include "fat12.h"
#include "lib/string.h"
#include "drivers/serial.h"

/*
 * 64 KB FAT12 ramdisk layout:
 *   Sector size:        512 bytes
 *   Sectors/cluster:    1
 *   Reserved sectors:   1  (BPB at sector 0)
 *   Number of FATs:     1
 *   Sectors per FAT:    1  (sector 1)
 *   Root dir entries:   16 (sector 2, 1 sector = 512 / 32 = 16 entries)
 *   Data start:         sector 3  (cluster 2 = sector 3)
 *   Total sectors:      128
 */

#define SECTOR_SIZE         512
#define TOTAL_SECTORS       (RAMDISK_SIZE / SECTOR_SIZE)
#define RESERVED_SECTORS    1
#define NUM_FATS            1
#define SECTORS_PER_FAT     1
#define ROOT_DIR_ENTRIES    16
#define SECTORS_PER_CLUSTER 1

#define FAT_OFFSET          (RESERVED_SECTORS * SECTOR_SIZE)
#define ROOTDIR_OFFSET      ((RESERVED_SECTORS + NUM_FATS * SECTORS_PER_FAT) * SECTOR_SIZE)
#define DATA_OFFSET         (ROOTDIR_OFFSET + ((ROOT_DIR_ENTRIES * 32 + SECTOR_SIZE - 1) / SECTOR_SIZE) * SECTOR_SIZE)

static uint8_t ramdisk[RAMDISK_SIZE];

static void write16(uint8_t *p, uint16_t val) {
    p[0] = val & 0xFF;
    p[1] = (val >> 8) & 0xFF;
}

static void write32(uint8_t *p, uint32_t val) {
    p[0] = val & 0xFF;
    p[1] = (val >> 8) & 0xFF;
    p[2] = (val >> 16) & 0xFF;
    p[3] = (val >> 24) & 0xFF;
}

static uint16_t next_free_cluster = 2;

/* Write a 12-bit FAT entry */
static void fat12_set_fat(uint16_t cluster, uint16_t value) {
    uint32_t offset = FAT_OFFSET + cluster + (cluster / 2);
    uint8_t *p = ramdisk + offset;

    if (cluster & 1) {
        p[0] = (p[0] & 0x0F) | ((value & 0x0F) << 4);
        p[1] = (value >> 4) & 0xFF;
    } else {
        p[0] = value & 0xFF;
        p[1] = (p[1] & 0xF0) | ((value >> 8) & 0x0F);
    }
}

static int next_dirent = 0;

/* Add a file to the ramdisk */
static void add_file(const char *name, const char *ext, const uint8_t *data, uint32_t size) {
    if (next_dirent >= ROOT_DIR_ENTRIES) return;

    /* Allocate clusters */
    uint16_t first_cluster = next_free_cluster;
    uint32_t bytes_left = size;
    uint32_t data_written = 0;
    uint32_t cluster_size = SECTORS_PER_CLUSTER * SECTOR_SIZE;

    while (bytes_left > 0) {
        uint16_t cur = next_free_cluster++;
        uint32_t offset = DATA_OFFSET + (cur - 2) * cluster_size;
        uint32_t chunk = bytes_left < cluster_size ? bytes_left : cluster_size;
        memcpy(ramdisk + offset, data + data_written, chunk);
        data_written += chunk;
        bytes_left -= chunk;

        if (bytes_left > 0) {
            fat12_set_fat(cur, next_free_cluster);  /* Point to next */
        } else {
            fat12_set_fat(cur, 0xFFF);  /* End of chain */
        }
    }

    /* Write directory entry */
    uint8_t *dir = ramdisk + ROOTDIR_OFFSET + next_dirent * 32;
    memset(dir, ' ', 11);

    int i;
    for (i = 0; i < 8 && name[i]; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        dir[i] = c;
    }
    for (i = 0; i < 3 && ext[i]; i++) {
        char c = ext[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        dir[8 + i] = c;
    }

    dir[11] = 0x20;  /* Archive attribute */
    write16(dir + 26, first_cluster);
    write32(dir + 28, size);

    next_dirent++;
}

static void format_bpb(void) {
    uint8_t *bpb = ramdisk;

    /* Jump + NOP */
    bpb[0] = 0xEB; bpb[1] = 0x3C; bpb[2] = 0x90;

    /* OEM name */
    memcpy(bpb + 3, "RNBWOS  ", 8);

    write16(bpb + 11, SECTOR_SIZE);
    bpb[13] = SECTORS_PER_CLUSTER;
    write16(bpb + 14, RESERVED_SECTORS);
    bpb[16] = NUM_FATS;
    write16(bpb + 17, ROOT_DIR_ENTRIES);
    write16(bpb + 19, TOTAL_SECTORS);
    bpb[21] = 0xF0;                    /* Media descriptor: 3.5" floppy */
    write16(bpb + 22, SECTORS_PER_FAT);
    write16(bpb + 24, 18);             /* Sectors per track */
    write16(bpb + 26, 2);              /* Number of heads */
    write32(bpb + 28, 0);              /* Hidden sectors */
    write32(bpb + 32, 0);              /* Total sectors 32-bit (0 = use 16-bit field) */
}

void ramdisk_init(void) {
    memset(ramdisk, 0, RAMDISK_SIZE);

    /* Format BPB */
    format_bpb();

    /* Set FAT media byte: entries 0 and 1 are reserved */
    fat12_set_fat(0, 0xFF0);
    fat12_set_fat(1, 0xFFF);
    next_free_cluster = 2;
    next_dirent = 0;

    /* Add sample files */
    static const char readme[] =
        "Welcome to Rainbow-OS!\r\n"
        "\r\n"
        "This is a 32-bit operating system for the Intel 486.\r\n"
        "It runs in Protected Mode with paging enabled.\r\n"
        "\r\n"
        "Type 'help' for available commands.\r\n";

    static const char hello[] =
        "Hello, World!\r\n"
        "Greetings from the FAT12 ramdisk.\r\n";

    static const char colors[] =
        "Rainbow Colors:\r\n"
        "  Red\r\n"
        "  Orange\r\n"
        "  Yellow\r\n"
        "  Green\r\n"
        "  Blue\r\n"
        "  Indigo\r\n"
        "  Violet\r\n";

    add_file("readme",  "txt", (const uint8_t *)readme,  strlen(readme));
    add_file("hello",   "txt", (const uint8_t *)hello,   strlen(hello));
    add_file("colors",  "txt", (const uint8_t *)colors,  strlen(colors));

    /* Initialize FAT12 driver with this volume */
    fat12_init(ramdisk, RAMDISK_SIZE);

    serial_log("FAT12 ramdisk ready (64 KB, 3 files)");
}
