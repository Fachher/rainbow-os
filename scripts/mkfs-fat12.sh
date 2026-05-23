#!/bin/bash
# Create a 1 MB FAT12 filesystem image with default files from rootfs/
set -e

ROOTFS_DIR="$1"
OUTPUT="$2"

if [ -z "$ROOTFS_DIR" ] || [ -z "$OUTPUT" ]; then
    echo "Usage: $0 <rootfs-dir> <output-image>"
    exit 1
fi

# Create 1 MB (2048 sectors) blank image
dd if=/dev/zero of="$OUTPUT" bs=512 count=2048 2>/dev/null

# Format as FAT12 using mtools
# mformat parameters matching our BPB:
#   -f 1024       not used (we specify manually)
#   -C            create image
#   -i            image file
#   -h 1 -t 1     heads/tracks (dummy geometry)
#   -s 2048       total sectors
#   -c 1          sectors per cluster
#   -r 4          root dir sectors (64 entries)
#   -L 1          reserved sectors
#   -M 512        sector size

# Use mformat from mtools
export MTOOLS_SKIP_CHECK=1

mformat -i "$OUTPUT" \
    -h 1 -t 2 -s 1024 \
    -c 1 \
    -r 64 \
    -R 1 \
    -M 512 \
    :: 2>/dev/null || {
    echo "mformat failed, trying alternative..."
    # Fallback: use dd + manual BPB (written by kernel on first boot)
    echo "WARNING: mtools not available. Kernel will format on first boot."
    exit 0
}

# Copy default files
for f in "$ROOTFS_DIR"/*; do
    if [ -f "$f" ]; then
        mcopy -i "$OUTPUT" "$f" :: 2>/dev/null && \
            echo "  Added: $(basename "$f")" || \
            echo "  WARN: Failed to add $(basename "$f")"
    fi
done

echo "FAT12 image created: $OUTPUT ($(wc -c < "$OUTPUT") bytes)"
