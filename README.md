# Rainbow-OS

A custom 32-bit operating system for the Intel 486, emulated with QEMU. It boots
from a hand-written two-stage NASM bootloader into protected mode and runs a
Rust kernel. Higher-level subsystems (text editor, BASIC interpreter, and a small
C compiler) are written in C and linked against the kernel.

## Features

- **Two-stage bootloader** (NASM) — real mode → protected mode, boots from HDD (IDE) with floppy fallback
- **Rust kernel** — freestanding, custom `i686-rainbow-os` target (i486, no SSE/MMX)
- **Interrupts** — IDT, PIC remapping, ISR stubs
- **Memory management** — physical memory manager (PMM) and paging
- **Drivers** — VGA text mode (80×25), PS/2 keyboard, COM1 serial, ATA PIO disk, Cirrus GD5446 SVGA graphics
- **Filesystem** — FAT12 on a RAM disk and disk-backed storage
- **Interactive shell** — built-in commands plus launchers for the C subsystems
- **Vim-like text editor** (C) — word motions, operators, counts
- **BASIC interpreter** (C) — tokenizer, expression evaluator, variables
- **C compiler** (C) — lexer, parser, codegen, preprocessor, supports `printf` and syscall intrinsics

## Target Platform (QEMU)

```
CPU:     Intel 486, 66 MHz   (-cpu 486)
RAM:     32 MB               (-m 32)
Graphics: Cirrus GD5446      (-vga cirrus)
Boot:    HDD image (IDE), or 1.44 MB floppy fallback
Serial:  COM1 on stdio       (-serial stdio)
Debug:   GDB stub on port 1234 (-s -S)
```

## Build

### Prerequisites

```bash
brew install i686-elf-gcc i686-elf-binutils nasm qemu mtools
# optional, for remote debugging:
brew install i686-elf-gdb
# Rust nightly with rust-src for the custom target:
rustup toolchain install nightly
rustup component add rust-src --toolchain nightly
```

### Compile

```bash
# Configure with the cross-compiler toolchain
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/i686-elf-toolchain.cmake

# Build bootloader + Rust kernel + C subsystems + disk images
cmake --build build
```

The build links the Rust kernel static library, the NASM bootloader, and the C
subsystem object files with `i686-elf-ld`.

## Run

```bash
./scripts/run.sh          # boot from the HDD image (IDE)
./scripts/run-hdd.sh      # explicit HDD boot
./scripts/debug.sh        # boot with GDB stub, waits for a debugger

# headless:
timeout 10 qemu-system-i386 -cpu 486 -m 32 -vga cirrus \
  -drive file=build/rainbow-os-hdd.img,format=raw,if=ide \
  -serial stdio -display none
```

## Shell Commands

| Command         | Description                                   |
|-----------------|-----------------------------------------------|
| `help`          | List available commands                       |
| `ls`            | List files on the FAT12 filesystem            |
| `cat <file>`    | Print a file's contents                       |
| `rm <file>`     | Delete a file                                 |
| `edit [file]`   | Open the vim-like text editor                 |
| `basic`         | Start the BASIC interpreter                   |
| `cc <file>`     | Compile a C source file                       |
| `run <file>`    | Run a compiled program                        |
| `gfx`           | SVGA graphics demo                            |
| `meminfo`       | Show memory usage                             |
| `sync`          | Flush the filesystem to disk                  |
| `clear`         | Clear the screen                              |
| `version`       | Show version info                             |
| `reboot` / `shutdown` | Reboot or power off                     |

## Project Layout

```
boot/              Two-stage NASM bootloader (boot.asm, stage2.asm)
kernel/
  entry.asm        Kernel entry, zeroes BSS, calls kernel_main
  rust/            Rust kernel
    src/           io, serial, vga, pic, idt, pmm, paging, keyboard,
                   ata, fat12, ramdisk, diskfs, svga, shell, string
    i686-rainbow-os.json   Custom build target
  editor/          Vim-like text editor (C)
  basic/           BASIC interpreter (C)
  cc/              C compiler (C)
  drivers/ fs/ lib/ include/   C headers shared with subsystems
cmake/             i686-elf cross-compiler toolchain file
scripts/           Build and run helpers (mkimage, run, debug)
docs/              Design notes and plans
```

## Disk Layout (HDD image)

| Sectors   | Contents                  |
|-----------|---------------------------|
| 0         | MBR                       |
| 1–4       | Stage 2 bootloader        |
| 5–516     | Kernel (512 sectors max, 256 KB) |
| 517       | Reserved                  |
| 518+      | FAT12 filesystem          |

## Notes

- The codebase started in German; comments may be in German, identifiers are English.
- Host is an Apple Silicon Mac — local Clang diagnostics (missing `types.h`,
  pointer-to-int casts) are false positives from the ARM host and can be ignored;
  the cross-compiler builds cleanly.
- See `handoff.md` for milestone history and detailed architecture notes.
