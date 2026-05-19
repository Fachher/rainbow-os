#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/../build"

echo "Starting QEMU with GDB stub on port 1234..."
echo "Connect CLion GDB Remote Debug to localhost:1234"
echo "Symbol file: $BUILD_DIR/kernel.elf"

qemu-system-i386 \
    -cpu 486 \
    -m 32 \
    -vga cirrus \
    -drive file="$BUILD_DIR/rainbow-os.img",format=raw,if=floppy \
    -serial stdio \
    -no-reboot \
    -no-shutdown \
    -s -S \
    -d guest_errors
