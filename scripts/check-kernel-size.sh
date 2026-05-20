#!/bin/bash
# Check that kernel.bin does not exceed the max size loaded by stage2
KERNEL_BIN="$1"
MAX_SIZE="$2"

SIZE=$(wc -c < "$KERNEL_BIN" | tr -d ' ')
if [ "$SIZE" -gt "$MAX_SIZE" ]; then
    echo "ERROR: kernel.bin is $SIZE bytes, exceeds $MAX_SIZE (KERNEL_SECTORS=64). Increase KERNEL_SECTORS in stage2.asm." >&2
    exit 1
fi
