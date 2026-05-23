# Persistent File Storage - Implementation Plan

## Overview

Add persistent file storage to Rainbow-OS by placing a FAT12 filesystem
partition on the HDD image (after the kernel region) and implementing an
ATA PIO driver to read/write it from Protected Mode.

## 1. HDD Image Layout

Current layout (16 MB = 32768 sectors of 512 bytes):

| Sector | Content |
|--------|---------|
| 0 | MBR / Stage 1 bootloader |
| 1-4 | Stage 2 bootloader |
| 5-260 | Kernel (256 sectors = 128 KB max) |
| 261-32767 | **UNUSED** |

New layout:

| Sector | Content |
|--------|---------|
| 0 | MBR / Stage 1 bootloader |
| 1-4 | Stage 2 bootloader |
| 5-260 | Kernel (256 sectors, 128 KB) |
| 261 | Reserved (alignment/marker) |
| 262-2309 | **FAT12 persistent partition** (2048 sectors = 1 MB) |
| 2310-32767 | Free (future expansion) |

Constants:
- `HDD_FS_START_SECTOR  262` (LBA, 0-indexed)
- `HDD_FS_SECTOR_COUNT  2048` (1 MB)
- The partition is at a fixed known offset (not an MBR partition entry).

FAT12 volume parameters:
- Bytes/sector: 512
- Sectors/cluster: 1 (512 bytes, fine for small files)
- Reserved sectors: 1
- FATs: 1
- Sectors per FAT: 3 (supports ~2000 clusters)
- Root dir entries: 64 (4 sectors)
- Total sectors: 2048
- Media byte: 0xF8 (hard disk)
- Data start: sector 8 (1 reserved + 3 FAT + 4 root dir)
- Usable data: ~2040 sectors = ~1 MB

## 2. ATA PIO Driver

**New files:** `kernel/drivers/ata.c`, `kernel/drivers/ata.h`

### 2.1 Interface

```c
/* Initialize ATA driver (identify primary master drive) */
int ata_init(void);

/* Read sectors using 28-bit LBA PIO */
int ata_read_sectors(uint32_t lba, uint8_t count, uint8_t *buf);

/* Write sectors using 28-bit LBA PIO */
int ata_write_sectors(uint32_t lba, uint8_t count, const uint8_t *buf);
```

### 2.2 Implementation Details

Standard ATA PIO ports for primary bus:

| Port | Register |
|------|----------|
| 0x1F0 | Data (16-bit read/write) |
| 0x1F1 | Error |
| 0x1F2 | Sector Count |
| 0x1F3 | LBA Low |
| 0x1F4 | LBA Mid |
| 0x1F5 | LBA High |
| 0x1F6 | Drive/Head |
| 0x1F7 | Command/Status |
| 0x3F6 | Alt Status |

**ata_init():**
1. Select drive 0 (master): `outb(0x1F6, 0xA0)`
2. Write 0 to sector count, LBA lo/mid/hi
3. Send IDENTIFY command (0xEC) to 0x1F7
4. Poll status until BSY clears
5. Read 256 words from 0x1F0 (validate drive exists)
6. Return 0 on success, -1 if no drive

**ata_read_sectors(lba, count, buf):**
1. Wait for BSY to clear
2. Set drive/LBA: `outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F))`
3. Set count, LBA lo/mid/hi
4. Send READ SECTORS command (0x20)
5. For each sector: poll DRQ, read 256 words via `inw(0x1F0)`

**ata_write_sectors(lba, count, buf):**
1. Same setup, command 0x30 (WRITE SECTORS)
2. For each sector: poll DRQ, write 256 words via `outw(0x1F0)`
3. After last sector: send FLUSH CACHE (0xE7), wait BSY clear

### 2.3 Prerequisites

Add `inw()` and `outw()` to `kernel/include/io.h`:

```c
static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}
```

### 2.4 Polling vs Interrupts

Use polling (not IRQ 14). The OS is single-threaded, disk I/O is
infrequent and user-initiated. Polling is simpler and sufficient.

## 3. Disk-backed FAT12 Integration

### 3.1 Architecture: Buffer Cache

**New files:** `kernel/fs/diskfs.c`, `kernel/fs/diskfs.h`

The existing FAT12 driver operates on a `uint8_t *vol` pointer.
Strategy:

1. Allocate `static uint8_t diskfs_volume[1 MB]` in BSS
2. On boot, read the HDD partition into this buffer via ATA
3. Call `fat12_init(diskfs_volume, 1MB)` — existing driver works unchanged
4. On writes, flush buffer back to disk via ATA

```c
#define DISKFS_SIZE (1024 * 1024)  /* 1 MB */

int diskfs_init(void);   /* Read partition from HDD, init FAT12 */
int diskfs_sync(void);   /* Write buffer back to HDD */
```

### 3.2 Write Persistence

Add wrapper functions in diskfs.c that auto-sync after writes:

```c
int diskfs_write_file(const char *name, const uint8_t *data, uint32_t size) {
    int result = fat12_write_file(name, data, size);
    if (result == 0) diskfs_sync();
    return result;
}

int diskfs_delete_file(const char *name) {
    int result = fat12_delete_file(name);
    if (result == 0) diskfs_sync();
    return result;
}
```

Update callers (~5 call sites: editor.c, shell.c, basic.c, cc.c) to use
`diskfs_write_file()` instead of `fat12_write_file()`.

### 3.3 Floppy Fallback

If ATA init fails (booting from floppy, no HDD):
- Fall back to `ramdisk_init()` (in-memory, non-persistent)
- All code continues to work, just without persistence

## 4. Default Files Handling

### Strategy: Bake defaults into HDD image at build time

Move hardcoded file strings from `ramdisk.c` into actual files:

```
rootfs/
├── readme.txt
├── hello.txt
├── colors.txt
├── crt0.h
└── hello.c
```

Build script `scripts/mkfs-fat12.sh` creates a 1 MB FAT12 image
with these files, then `mkimage-hdd.sh` embeds it at sector 262.

### First-boot detection

`diskfs_init()` validates the loaded volume by checking for a valid
BPB (jump instruction at byte 0, bytes_per_sector == 512). If invalid,
it formats a fresh FAT12 in the buffer, populates defaults, and syncs.

## 5. Shell Commands

### Existing (no change needed):
- `ls` — calls `fat12_list_root()`, works transparently
- `cat FILE` — calls `fat12_read_file()`, works transparently
- `edit FILE` — needs `diskfs_write_file()` wrapper for persistence

### New:
- `sync` — explicit flush: `diskfs_sync()`, shows "Synced to disk"
- `rm FILE` — delete file: `diskfs_delete_file()` (API already exists, no shell command yet)

## 6. Boot Flow

### Current:
```
kernel_main() -> ramdisk_init() -> fat12_init(ramdisk, 256K)
```

### New:
```
kernel_main() -> diskfs_init()
                    |-> ata_init()
                    |-> ata_read_sectors() [load 1 MB partition]
                    |-> validate BPB
                    |   |-> valid: fat12_init(diskfs_volume, 1M)
                    |   |-> invalid: format + populate defaults + sync
                    |-> fallback on ATA failure: ramdisk_init()
```

Status messages:
- `[OK] Persistent storage ready (ATA, 1 MB FAT12)` — HDD success
- `[OK] FAT12 ramdisk ready (no disk)` — floppy fallback

## 7. Build System Changes

### CMakeLists.txt:
- Add `kernel/drivers/ata.c` and `kernel/fs/diskfs.c` to `KERNEL_SOURCES`
- Add target to build FAT12 filesystem image

### New files:

```
kernel/drivers/ata.c
kernel/drivers/ata.h
kernel/fs/diskfs.c
kernel/fs/diskfs.h
rootfs/readme.txt
rootfs/hello.txt
rootfs/colors.txt
rootfs/crt0.h
rootfs/hello.c
scripts/mkfs-fat12.sh
```

### HDD image rebuild safety:

`mkimage-hdd.sh` should only overwrite boot+kernel sectors (0-260)
on rebuild, preserving the filesystem partition. Only write the fs
partition if explicitly requested or if no valid FAT12 exists at
sector 262.

## 8. Implementation Phases

### Phase 1: ATA PIO Driver (~200 lines)
1. Add `inw()`/`outw()` to `io.h`
2. Create `ata.h` / `ata.c`
3. Test: IDENTIFY succeeds, read MBR sector, verify 0xAA55

### Phase 2: Build-time Filesystem (~50 lines script)
1. Create `rootfs/` with default files
2. Create `scripts/mkfs-fat12.sh`
3. Update `mkimage-hdd.sh` to embed partition
4. Test: hexdump sector 262, verify BPB

### Phase 3: Disk FS Module (~150 lines)
1. Create `diskfs.h` / `diskfs.c`
2. Implement `diskfs_init()`, `diskfs_sync()`
3. Replace `ramdisk_init()` with `diskfs_init()` in `kernel.c`
4. Test: `ls` shows files from disk

### Phase 4: Write Persistence (~50 lines)
1. Add `diskfs_write_file()` / `diskfs_delete_file()` wrappers
2. Update callers (editor, basic, cc, shell)
3. Add `sync` and `rm` shell commands
4. Test: edit file -> reboot -> file persists

### Phase 5: Cleanup
1. Remove hardcoded strings from `ramdisk.c`
2. Update help text
3. Update `handoff.md`

## 9. Memory Budget

| Buffer | Current | After |
|--------|---------|-------|
| ramdisk[] | 256 KB | 0 (removed when diskfs active) |
| diskfs_volume[] | — | 1 MB |
| **Net change** | | **+768 KB BSS** |
| Total kernel BSS | ~300 KB | ~1.1 MB |
| Free RAM (32 MB) | ~31 MB | ~30.3 MB |

## 10. Risks

| Risk | Mitigation |
|------|-----------|
| ATA PIO not working on QEMU | Universally supported. Test early in Phase 1. |
| Data corruption on unclean shutdown | Accept for v1. Explicit `sync` command. |
| mkfs.fat/mtools not on macOS | Use `newfs_msdos` (built-in). `brew install mtools` for mcopy. |
| HDD image rebuilt destroys data | Only overwrite boot+kernel sectors, preserve fs partition. |
| Floppy-only boot breaks | Fallback to ramdisk. Detect via ata_init() failure. |

## Estimated Complexity

| Phase | Lines | Key challenge |
|-------|-------|---------------|
| 1. ATA driver | ~200 | Correct PIO timing, polling |
| 2. Build scripts | ~50 | Cross-platform FAT12 creation (macOS/Linux) |
| 3. Disk FS module | ~150 | BPB validation, first-boot formatting |
| 4. Write persistence | ~50 | Finding and updating all write call sites |
| 5. Cleanup | ~20 | — |
| **Total** | **~470** | |
