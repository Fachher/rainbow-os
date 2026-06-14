#!/bin/bash
# Build a Rainbow-OS ring-3 user program into a flat binary at 0x200000.
# Usage: build-userapp.sh <repo-root> <build-dir> <app-name>
set -e

SRC="$1"; BUILD="$2"; APP="$3"
if [ -z "$SRC" ] || [ -z "$BUILD" ] || [ -z "$APP" ]; then
    echo "Usage: $0 <repo-root> <build-dir> <app-name>"; exit 1
fi

CC=i686-elf-gcc
LD=i686-elf-ld
OBJCOPY=i686-elf-objcopy
# -march=i486 matches the kernel: a generic i686 -O2 build emits cmov/SSE that
# the emulated 486 traps as #UD.
CFLAGS="-m32 -march=i486 -ffreestanding -fno-builtin -fno-pic -fno-stack-protector -fno-asynchronous-unwind-tables -O2 -Wall -I$SRC/apps/lib"

OBJ="$BUILD/userobj"
mkdir -p "$OBJ"

nasm -f elf32 "$SRC/apps/lib/crt0.asm"    -o "$OBJ/crt0.o"
nasm -f elf32 "$SRC/apps/lib/syscall.asm" -o "$OBJ/syscall.o"
$CC $CFLAGS -c "$SRC/apps/lib/umem.c"     -o "$OBJ/umem.o"
$CC $CFLAGS -c "$SRC/apps/$APP/$APP.c"    -o "$OBJ/$APP.o"

$LD -T "$SRC/apps/lib/user.ld" -o "$BUILD/$APP.elf" \
    "$OBJ/crt0.o" "$OBJ/$APP.o" "$OBJ/umem.o" "$OBJ/syscall.o"
$OBJCOPY -O binary "$BUILD/$APP.elf" "$BUILD/$APP.bin"

echo "  User app: $APP.bin ($(wc -c < "$BUILD/$APP.bin") bytes)"
