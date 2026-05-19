# Rainbow-OS Development Guide

## Project
Custom 32-bit OS for Intel 486 (QEMU). NASM bootloader + C kernel.

## Build
```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/i686-elf-toolchain.cmake
cmake --build build
./scripts/run.sh
```

## Conventions
- Language: German comments are OK (project started in German), code/identifiers in English
- Cross-compiler: `i686-elf-gcc` — local Clang warnings (types.h not found, pointer-to-int casts) are false positives from ARM host
- Kernel includes: use `"include/types.h"`, `"include/io.h"` from kernel/ root; drivers use `"drivers/foo.h"`
- No stdlib — freestanding: `#include` only project headers, no libc
- All new kernel C/ASM files must be added to `KERNEL_SOURCES` in `CMakeLists.txt`

## Workflow
- After completing each milestone, create a git commit
- Test with QEMU before committing (`./scripts/run.sh` or headless with `-display none`)
- See `handoff.md` for milestone status and architecture details
