cmake_minimum_required(VERSION 3.20)
project(rainbow-os C ASM_NASM)

set(CMAKE_ASM_NASM_OBJECT_FORMAT elf32)
set(CMAKE_ASM_NASM_FLAGS "-g -F dwarf")

# --- Kernel sources ---
set(KERNEL_SOURCES
    kernel/entry.asm
    kernel/isr_stubs.asm
    kernel/kernel.c
    kernel/idt.c
    kernel/pic.c
    kernel/pmm.c
    kernel/paging.c
    kernel/drivers/vga.c
    kernel/drivers/serial.c
    kernel/drivers/keyboard.c
    kernel/drivers/svga.c
    kernel/fs/fat12.c
    kernel/fs/ramdisk.c
    kernel/shell/shell.c
    kernel/lib/string.c
    kernel/editor/buffer.c
    kernel/editor/view.c
    kernel/editor/editor.c
    kernel/basic/program.c
    kernel/basic/tokenizer.c
    kernel/basic/vars.c
    kernel/basic/expr.c
    kernel/basic/exec.c
    kernel/basic/basic.c
)

add_executable(kernel.elf ${KERNEL_SOURCES})

target_include_directories(kernel.elf PRIVATE
    ${CMAKE_SOURCE_DIR}/kernel
    ${CMAKE_SOURCE_DIR}/kernel/include
    ${CMAKE_SOURCE_DIR}/kernel/drivers
    ${CMAKE_SOURCE_DIR}/kernel/fs
    ${CMAKE_SOURCE_DIR}/kernel/shell
    ${CMAKE_SOURCE_DIR}/kernel/lib
    ${CMAKE_SOURCE_DIR}/kernel/editor
    ${CMAKE_SOURCE_DIR}/kernel/basic
)

set_target_properties(kernel.elf PROPERTIES
    LINK_FLAGS "-T ${CMAKE_SOURCE_DIR}/kernel/linker.ld -nostdlib"
    LINK_DEPENDS "${CMAKE_SOURCE_DIR}/kernel/linker.ld"
)

# --- kernel.bin from kernel.elf ---
add_custom_command(TARGET kernel.elf POST_BUILD
    COMMAND i686-elf-objcopy -O binary $<TARGET_FILE:kernel.elf> ${CMAKE_BINARY_DIR}/kernel.bin
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/check-kernel-size.sh ${CMAKE_BINARY_DIR}/kernel.bin 65536
    COMMENT "Creating kernel.bin"
)

# --- Bootloader (NASM flat binary) ---
add_custom_target(bootloader ALL
    COMMAND nasm -f bin ${CMAKE_SOURCE_DIR}/boot/boot.asm -o ${CMAKE_BINARY_DIR}/boot.bin
    COMMAND nasm -f bin ${CMAKE_SOURCE_DIR}/boot/stage2.asm -o ${CMAKE_BINARY_DIR}/stage2.bin
    COMMENT "Assembling bootloader"
    SOURCES boot/boot.asm boot/stage2.asm
)

# --- Floppy image ---
add_custom_target(floppy ALL
    DEPENDS kernel.elf bootloader
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/mkimage.sh
        ${CMAKE_BINARY_DIR}/boot.bin
        ${CMAKE_BINARY_DIR}/stage2.bin
        ${CMAKE_BINARY_DIR}/kernel.bin
        ${CMAKE_BINARY_DIR}/rainbow-os.img
    COMMENT "Creating floppy image"
)

# --- HDD image ---
add_custom_target(hdd ALL
    DEPENDS kernel.elf bootloader
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/mkimage-hdd.sh
        ${CMAKE_BINARY_DIR}/boot.bin
        ${CMAKE_BINARY_DIR}/stage2.bin
        ${CMAKE_BINARY_DIR}/kernel.bin
        ${CMAKE_BINARY_DIR}/rainbow-os-hdd.img
    COMMENT "Creating HDD image"
)
