set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR i686)

set(CMAKE_C_COMPILER i686-elf-gcc)
set(CMAKE_LINKER i686-elf-ld)
set(CMAKE_OBJCOPY i686-elf-objcopy)
set(CMAKE_ASM_NASM_COMPILER nasm)

set(CMAKE_C_FLAGS_INIT "-ffreestanding -nostdlib -nostdinc -fno-builtin -fno-stack-protector -m32 -march=i486 -Wall -Wextra -g")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-nostdlib")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
