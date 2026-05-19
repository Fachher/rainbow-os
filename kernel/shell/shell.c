#include "shell.h"
#include "drivers/vga.h"
#include "drivers/serial.h"
#include "lib/string.h"
#include "include/io.h"
#include "include/pmm.h"
#include "fs/fat12.h"

#define CMD_MAX_LEN 78
#define PROMPT_STR  "> "

static char cmd_buf[CMD_MAX_LEN + 1];
static uint32_t cmd_len;

static void shell_prompt(void) {
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_write(PROMPT_STR);
    vga_set_color(VGA_WHITE, VGA_BLACK);
}

static void shell_execute(const char *cmd) {
    if (cmd[0] == '\0') {
        /* Empty command */
    } else if (strcmp(cmd, "help") == 0) {
        vga_write("Available commands:\n");
        vga_write("  help     - Show this help\n");
        vga_write("  clear    - Clear the screen\n");
        vga_write("  version  - Show system info\n");
        vga_write("  meminfo  - Show memory usage\n");
        vga_write("  ls       - List files\n");
        vga_write("  cat FILE - Show file contents\n");
        vga_write("  reboot   - Reboot the system\n");
    } else if (strcmp(cmd, "clear") == 0) {
        vga_clear();
    } else if (strcmp(cmd, "version") == 0) {
        vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        vga_write("Rainbow-OS v0.1\n");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        vga_write("CPU: Intel 486, 66 MHz\n");
        vga_write("RAM: 32 MB\n");
        vga_write("GPU: Cirrus Logic GD5446\n");
    } else if (strcmp(cmd, "meminfo") == 0) {
        uint32_t free_kb = pmm_free_count() * 4;
        uint32_t used_kb = pmm_used_count() * 4;
        uint32_t total_kb = TOTAL_MEMORY / 1024;
        vga_write("Memory: ");
        vga_write_dec(total_kb);
        vga_write(" KB total, ");
        vga_write_dec(used_kb);
        vga_write(" KB used, ");
        vga_write_dec(free_kb);
        vga_write(" KB free\n");
        vga_write("Pages:  ");
        vga_write_dec(pmm_used_count());
        vga_write(" used, ");
        vga_write_dec(pmm_free_count());
        vga_write(" free (4 KB each)\n");
    } else if (strcmp(cmd, "ls") == 0) {
        struct fat12_dirent entries[FAT12_MAX_FILES];
        int count = fat12_list_root(entries, FAT12_MAX_FILES);
        for (int i = 0; i < count; i++) {
            vga_write("  ");
            vga_write(entries[i].name);
            /* Pad to column 16 */
            int pad = 14 - (int)strlen(entries[i].name);
            while (pad-- > 0) vga_putchar(' ');
            vga_write_dec(entries[i].size);
            vga_write(" bytes\n");
        }
    } else if (strncmp(cmd, "cat ", 4) == 0) {
        const char *filename = cmd + 4;
        while (*filename == ' ') filename++;
        if (*filename == '\0') {
            vga_write("Usage: cat <filename>\n");
        } else {
            static uint8_t file_buf[4096];
            int bytes = fat12_read_file(filename, file_buf, sizeof(file_buf) - 1);
            if (bytes < 0) {
                vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
                vga_write("File not found: ");
                vga_set_color(VGA_WHITE, VGA_BLACK);
                vga_write(filename);
                vga_putchar('\n');
            } else {
                file_buf[bytes] = '\0';
                /* Print, converting \r\n to \n */
                for (int i = 0; i < bytes; i++) {
                    if (file_buf[i] == '\r') continue;
                    vga_putchar(file_buf[i]);
                }
            }
        }
    } else if (strcmp(cmd, "reboot") == 0) {
        vga_write("Rebooting...\n");
        serial_write("Rebooting...\n");
        /* Triple-fault: load null IDT, then trigger interrupt */
        struct { uint16_t limit; uint32_t base; } __attribute__((packed)) null_idt = {0, 0};
        __asm__ volatile("lidt %0; int $0x03" : : "m"(null_idt));
    } else {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_write("Unknown command: ");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        vga_write(cmd);
        vga_putchar('\n');
    }
}

void shell_init(void) {
    cmd_len = 0;
    cmd_buf[0] = '\0';
    shell_prompt();
}

void shell_putchar(char c) {
    if (c == '\n') {
        vga_putchar('\n');
        serial_putchar('\n');
        cmd_buf[cmd_len] = '\0';
        shell_execute(cmd_buf);
        cmd_len = 0;
        cmd_buf[0] = '\0';
        shell_prompt();
    } else if (c == '\b') {
        if (cmd_len > 0) {
            cmd_len--;
            vga_putchar('\b');
            serial_putchar('\b');
            serial_putchar(' ');
            serial_putchar('\b');
        }
    } else if (c == '\t') {
        /* Ignore tabs for now */
    } else if (c >= ' ' && cmd_len < CMD_MAX_LEN) {
        cmd_buf[cmd_len++] = c;
        vga_putchar(c);
        serial_putchar(c);
    }
}
