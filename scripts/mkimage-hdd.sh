#!/bin/bash
set -e

BOOT_BIN="$1"
STAGE2_BIN="$2"
KERNEL_BIN="$3"
OUTPUT="$4"

# 16 MB HDD image (standard CHS-aligned size)
dd if=/dev/zero of="$OUTPUT" bs=1M count=16 2>/dev/null

# Sector 0: MBR (Stage 1 bootloader)
dd if="$BOOT_BIN" of="$OUTPUT" bs=512 count=1 conv=notrunc 2>/dev/null

# Sectors 1-4: Stage 2 bootloader
dd if="$STAGE2_BIN" of="$OUTPUT" bs=512 seek=1 conv=notrunc 2>/dev/null

# Sectors 5+: Kernel binary
dd if="$KERNEL_BIN" of="$OUTPUT" bs=512 seek=5 conv=notrunc 2>/dev/null

echo "HDD image created: $OUTPUT"
echo "  Boot:   $(wc -c < "$BOOT_BIN") bytes"
echo "  Stage2: $(wc -c < "$STAGE2_BIN") bytes"
echo "  Kernel: $(wc -c < "$KERNEL_BIN") bytes"
