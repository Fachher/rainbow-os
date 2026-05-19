#include "drivers/vga.h"
#include "drivers/serial.h"
#include "drivers/keyboard.h"
#include "include/idt.h"
#include "include/pic.h"
#include "shell/shell.h"

void kernel_main(void) {
    /* Initialize drivers */
    serial_init();
    serial_write("=== Rainbow-OS Serial Console ===\n");
    serial_log("Serial port initialized");

    vga_init();
    serial_log("VGA initialized");

    /* Welcome message on screen */
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_write("  ____       _       _                       ___  ____  \n");
    vga_write(" |  _ \\ __ _(_)_ __ | |__   _____      __  / _ \\/ ___| \n");
    vga_write(" | |_) / _` | | '_ \\| '_ \\ / _ \\ \\ /\\ / / | | | \\___ \\ \n");
    vga_write(" |  _ < (_| | | | | | |_) | (_) \\ V  V /  | |_| |___) |\n");
    vga_write(" |_| \\_\\__,_|_|_| |_|_.__/ \\___/ \\_/\\_/    \\___/|____/ \n");

    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_write("\n");
    vga_write("Rainbow-OS v0.1 - 486/66MHz, 32MB RAM\n");
    vga_write("VGA Text Mode 80x25\n");
    vga_write("\n");

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_write("[OK] Kernel loaded successfully\n");
    vga_write("[OK] Serial console on COM1 (38400 baud)\n");

    /* Initialize interrupts */
    pic_init();
    idt_init();
    vga_write("[OK] Interrupts enabled (IDT + PIC)\n");

    /* Initialize keyboard */
    keyboard_init();
    vga_write("[OK] PS/2 keyboard ready\n");

    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_write("\n");

    serial_log("Kernel startup complete");
    serial_write("System: 486/66MHz, 32MB RAM, Cirrus GD5446\n");

    /* Start shell */
    shell_init();

    /* Halt loop - wakes on interrupt (keyboard), then halts again */
    while (1) {
        __asm__ volatile("hlt");
    }
}
