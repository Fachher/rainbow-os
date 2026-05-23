#include "ata.h"
#include "include/io.h"
#include "drivers/serial.h"

/* ATA primary bus I/O ports */
#define ATA_DATA        0x1F0
#define ATA_ERROR       0x1F1
#define ATA_SECT_COUNT  0x1F2
#define ATA_LBA_LO      0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HI      0x1F5
#define ATA_DRIVE_HEAD  0x1F6
#define ATA_CMD_STATUS  0x1F7
#define ATA_ALT_STATUS  0x3F6

/* ATA commands */
#define ATA_CMD_IDENTIFY    0xEC
#define ATA_CMD_READ        0x20
#define ATA_CMD_WRITE       0x30
#define ATA_CMD_FLUSH       0xE7

/* Status bits */
#define ATA_STATUS_BSY  0x80
#define ATA_STATUS_DRDY 0x40
#define ATA_STATUS_DRQ  0x08
#define ATA_STATUS_ERR  0x01

/* Wait for BSY to clear, return status. Timeout after ~1M iterations. */
static int ata_wait_bsy(void) {
    for (int i = 0; i < 1000000; i++) {
        uint8_t status = inb(ATA_ALT_STATUS);
        if (!(status & ATA_STATUS_BSY))
            return status;
    }
    return -1;
}

/* Wait for DRQ (data request) after BSY clears */
static int ata_wait_drq(void) {
    for (int i = 0; i < 1000000; i++) {
        uint8_t status = inb(ATA_ALT_STATUS);
        if (status & ATA_STATUS_ERR) return -1;
        if (!(status & ATA_STATUS_BSY) && (status & ATA_STATUS_DRQ))
            return 0;
    }
    return -1;
}

/* 400ns delay by reading alt status 4 times */
static void ata_delay(void) {
    inb(ATA_ALT_STATUS);
    inb(ATA_ALT_STATUS);
    inb(ATA_ALT_STATUS);
    inb(ATA_ALT_STATUS);
}

int ata_init(void) {
    /* Select master drive */
    outb(ATA_DRIVE_HEAD, 0xA0);
    ata_delay();

    /* Zero out sector count and LBA */
    outb(ATA_SECT_COUNT, 0);
    outb(ATA_LBA_LO, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HI, 0);

    /* Send IDENTIFY */
    outb(ATA_CMD_STATUS, ATA_CMD_IDENTIFY);
    ata_delay();

    /* Check if drive exists */
    uint8_t status = inb(ATA_ALT_STATUS);
    if (status == 0) {
        serial_log("ATA: no drive found");
        return -1;
    }

    /* Wait for BSY to clear */
    if (ata_wait_bsy() < 0) {
        serial_log("ATA: IDENTIFY timeout");
        return -1;
    }

    /* Check for non-ATA drive (LBA mid/hi should be 0) */
    if (inb(ATA_LBA_MID) != 0 || inb(ATA_LBA_HI) != 0) {
        serial_log("ATA: not an ATA drive");
        return -1;
    }

    /* Wait for DRQ */
    if (ata_wait_drq() < 0) {
        serial_log("ATA: IDENTIFY DRQ failed");
        return -1;
    }

    /* Read 256 words of identify data (discard) */
    for (int i = 0; i < 256; i++)
        inw(ATA_DATA);

    serial_log("ATA: primary master identified");
    return 0;
}

int ata_read_sectors(uint32_t lba, uint8_t count, uint8_t *buf) {
    if (count == 0) return -1;

    ata_wait_bsy();

    /* Select drive + LBA bits 24-27 */
    outb(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    ata_delay();

    outb(ATA_SECT_COUNT, count);
    outb(ATA_LBA_LO, lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HI, (lba >> 16) & 0xFF);

    /* Send READ SECTORS command */
    outb(ATA_CMD_STATUS, ATA_CMD_READ);

    for (int s = 0; s < count; s++) {
        if (ata_wait_drq() < 0) {
            serial_log("ATA: read DRQ timeout");
            return -1;
        }

        /* Read 256 words (512 bytes) */
        uint16_t *p = (uint16_t *)(buf + s * 512);
        for (int i = 0; i < 256; i++)
            p[i] = inw(ATA_DATA);
    }

    return 0;
}

int ata_write_sectors(uint32_t lba, uint8_t count, const uint8_t *buf) {
    if (count == 0) return -1;

    ata_wait_bsy();

    /* Select drive + LBA bits 24-27 */
    outb(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    ata_delay();

    outb(ATA_SECT_COUNT, count);
    outb(ATA_LBA_LO, lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HI, (lba >> 16) & 0xFF);

    /* Send WRITE SECTORS command */
    outb(ATA_CMD_STATUS, ATA_CMD_WRITE);

    for (int s = 0; s < count; s++) {
        if (ata_wait_drq() < 0) {
            serial_log("ATA: write DRQ timeout");
            return -1;
        }

        /* Write 256 words (512 bytes) */
        const uint16_t *p = (const uint16_t *)(buf + s * 512);
        for (int i = 0; i < 256; i++)
            outw(ATA_DATA, p[i]);
    }

    /* Flush cache */
    outb(ATA_CMD_STATUS, ATA_CMD_FLUSH);
    if (ata_wait_bsy() < 0) {
        serial_log("ATA: flush timeout");
        return -1;
    }

    return 0;
}
