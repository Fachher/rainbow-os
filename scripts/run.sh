#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/../build"

qemu-system-i386 \
    -cpu 486 \
    -m 32 \
    -vga cirrus \
    -drive file="$BUILD_DIR/rainbow-os.img",format=raw,if=floppy \
    -serial stdio \
    -no-shutdown \
    -d guest_errors
