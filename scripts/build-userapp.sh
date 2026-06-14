#!/bin/bash
# Build a Rainbow-OS ring-3 user program into a flat binary at 0x200000.
# Usage: build-userapp.sh <repo-root> <build-dir> <app-name>
set -e

SRC="$1"; BUILD="$2"; APP="$3"; OPT="${4:--O2}"
if [ -z "$SRC" ] || [ -z "$BUILD" ] || [ -z "$APP" ]; then
    echo "Usage: $0 <repo-root> <build-dir> <app-name> [opt-level]"; exit 1
fi

CC=i686-elf-gcc
LD=i686-elf-ld
OBJCOPY=i686-elf-objcopy
# -march=i486 matches the kernel: a generic i686 -O2 build emits cmov/SSE that
# the emulated 486 traps as #UD.
# -I$SRC/apps lets an app's own headers resolve as "<app>/header.h" (e.g. BASIC's
# "basic/program.h"); -I$SRC/apps/lib provides the SDK + kernel-API shim headers.
CFLAGS="-m32 -march=i486 -ffreestanding -fno-builtin -fno-pic -fno-stack-protector -fno-asynchronous-unwind-tables $OPT -Wall -I$SRC/apps -I$SRC/apps/lib"

OBJ="$BUILD/userobj/$APP"
mkdir -p "$OBJ"

# SDK objects (runtime + libc + syscall shims)
nasm -f elf32 "$SRC/apps/lib/crt0.asm"    -o "$OBJ/crt0.o"
nasm -f elf32 "$SRC/apps/lib/syscall.asm" -o "$OBJ/syscall.o"
$CC $CFLAGS -c "$SRC/apps/lib/umem.c"     -o "$OBJ/umem.o"
$CC $CFLAGS -c "$SRC/apps/lib/oscompat.c" -o "$OBJ/oscompat.o"

# All C sources of the app (crt0 first via the linker script's .start section)
OBJS="$OBJ/crt0.o $OBJ/syscall.o $OBJ/umem.o $OBJ/oscompat.o"
for src in "$SRC/apps/$APP/"*.c; do
    o="$OBJ/$(basename "$src" .c).o"
    $CC $CFLAGS -c "$src" -o "$o"
    OBJS="$OBJS $o"
done

$LD -T "$SRC/apps/lib/user.ld" -o "$BUILD/$APP.elf" $OBJS
$OBJCOPY -O binary "$BUILD/$APP.elf" "$BUILD/$APP.bin"

echo "  User app: $APP.bin ($(wc -c < "$BUILD/$APP.bin") bytes)"
