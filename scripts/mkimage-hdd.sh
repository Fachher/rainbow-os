#!/bin/bash
set -e

BOOT_BIN="$1"
STAGE2_BIN="$2"
KERNEL_BIN="$3"
OUTPUT="$4"
FS_IMAGE="$5"    # Optional: FAT12 filesystem image

FS_START_SECTOR=262
FS_SECTOR_COUNT=2048

# Create image only if it doesn't exist (preserve filesystem partition)
if [ ! -f "$OUTPUT" ]; then
    dd if=/dev/zero of="$OUTPUT" bs=1M count=16 2>/dev/null
fi

# Sector 0: MBR (Stage 1 bootloader)
dd if="$BOOT_BIN" of="$OUTPUT" bs=512 count=1 conv=notrunc 2>/dev/null

# Sectors 1-4: Stage 2 bootloader
dd if="$STAGE2_BIN" of="$OUTPUT" bs=512 seek=1 conv=notrunc 2>/dev/null

# Sectors 5+: Kernel binary
dd if="$KERNEL_BIN" of="$OUTPUT" bs=512 seek=5 conv=notrunc 2>/dev/null

# Embed FAT12 filesystem at sector 262 if provided
if [ -n "$FS_IMAGE" ] && [ -f "$FS_IMAGE" ]; then
    # Check if a valid FAT12 already exists at sector 262
    EXISTING=$(dd if="$OUTPUT" bs=512 skip=$FS_START_SECTOR count=1 2>/dev/null | od -A n -t x1 -N 3 | tr -d ' ')
    if [ "$EXISTING" != "eb3c90" ] && [ "$EXISTING" != "eb3e90" ]; then
        # No valid BPB found — write the filesystem image
        dd if="$FS_IMAGE" of="$OUTPUT" bs=512 seek=$FS_START_SECTOR conv=notrunc 2>/dev/null
        echo "  FS:     embedded at sector $FS_START_SECTOR ($(wc -c < "$FS_IMAGE") bytes)"
    else
        echo "  FS:     preserved existing partition at sector $FS_START_SECTOR"
    fi
fi

echo "HDD image created: $OUTPUT"
echo "  Boot:   $(wc -c < "$BOOT_BIN") bytes"
echo "  Stage2: $(wc -c < "$STAGE2_BIN") bytes"
echo "  Kernel: $(wc -c < "$KERNEL_BIN") bytes"
