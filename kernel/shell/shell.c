#include "shell.h"
#include "drivers/vga.h"
#include "drivers/serial.h"
#include "lib/string.h"
#include "include/pmm.h"
#include "fs/fat12.h"
#include "fs/diskfs.h"
#include "drivers/svga.h"
#include "drivers/keyboard.h"
#include "editor/editor.h"
#include "basic/basic.h"
#include "cc/cc.h"
#include "cc/runtime.h"
#include "game/asteroids.h"
#include "debug/debugger.h"
#include "include/io.h"

#define CMD_MAX_LEN 78
#define PROMPT_STR  "> "

static char cmd_buf[CMD_MAX_LEN + 1];
static uint32_t cmd_len;

static const char *const shell_commands[] = {
    "help", "clear", "version", "meminfo", "ls", "cat", "edit", "rm",
    "sync", "basic", "cc", "run", "debug", "gfx", "asteroids", "reboot", "shutdown", 0
};

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
        vga_write("  help      - Show this help\n");
        vga_write("  clear     - Clear the screen\n");
        vga_write("  version   - Show system info\n");
        vga_write("  meminfo   - Show memory usage\n");
        vga_write("  ls        - List files\n");
        vga_write("  cat FILE  - Show file contents\n");
        vga_write("  edit      - Text editor (vim-like)\n");
        vga_write("  rm FILE   - Delete a file\n");
        vga_write("  sync      - Flush filesystem to disk\n");
        vga_write("  basic     - BASIC interpreter\n");
        vga_write("  cc FILE   - Compile C file\n");
        vga_write("  cc FILE -r - Compile and run\n");
        vga_write("  run FILE  - Execute binary\n");
        vga_write("  debug FILE - Debug a binary (breakpoints/step)\n");
        vga_write("  gfx       - Graphics demo (800x600)\n");
        vga_write("  asteroids - Play Asteroids\n");
        vga_write("  reboot    - Reboot the system\n");
        vga_write("  shutdown  - Power off the system\n");
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
                if (bytes == (int)(sizeof(file_buf) - 1)) {
                    vga_set_color(VGA_YELLOW, VGA_BLACK);
                    vga_write("\n[truncated at 4095 bytes]\n");
                    vga_set_color(VGA_WHITE, VGA_BLACK);
                }
            }
        }
    } else if (strcmp(cmd, "gfx") == 0) {
        /* Already in 800x600x8bpp; switch to the full 256-color palette. */
        svga_rainbow_palette();
        svga_clear(0);

        /* Rainbow arc, centered in the lower half */
        for (int band = 0; band < 7; band++) {
            uint8_t color = 1 + band * 27;   /* Spread across rainbow palette */
            int r_outer = 260 - band * 26;
            int r_inner = r_outer - 24;
            int cx = SVGA_WIDTH / 2, cy = SVGA_HEIGHT - 120;
            for (int y = cy - r_outer; y <= cy; y++) {
                for (int x = cx - r_outer; x <= cx + r_outer; x++) {
                    int dx = x - cx, dy = y - cy;
                    int dist_sq = dx * dx + dy * dy;
                    if (dist_sq >= r_inner * r_inner && dist_sq <= r_outer * r_outer) {
                        svga_putpixel(x, y, color);
                    }
                }
            }
        }

        /* Color bars at bottom */
        int bar_w = SVGA_WIDTH / 192;
        if (bar_w < 1) bar_w = 1;
        for (int i = 0; i < 192; i++) {
            svga_fill_rect(i * bar_w + (SVGA_WIDTH - 192 * bar_w) / 2,
                          SVGA_HEIGHT - 50, bar_w, 40, 1 + i);
        }

        /* Title bar */
        svga_fill_rect(SVGA_WIDTH / 2 - 150, 30, 300, 40, 255);
        svga_fill_rect(SVGA_WIDTH / 2 - 147, 33, 294, 34, 0);

        /* Wait for any key to return to the console */
        serial_write("GFX demo active. Press any key to return.\n");
        keyboard_wait_any();

        /* Restore the console palette and repaint a blank console. */
        vga_reset_palette();
        vga_clear();

        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        vga_write("[OK] Returned to console\n");
        vga_set_color(VGA_WHITE, VGA_BLACK);
    } else if (strcmp(cmd, "asteroids") == 0) {
        asteroids_run();
    } else if (strcmp(cmd, "edit") == 0) {
        editor_open((const char *)0);
    } else if (strncmp(cmd, "edit ", 5) == 0) {
        const char *fname = cmd + 5;
        while (*fname == ' ') fname++;
        editor_open(fname);
    } else if (strcmp(cmd, "basic") == 0) {
        basic_run();
    } else if (strncmp(cmd, "basic ", 6) == 0) {
        const char *fname = cmd + 6;
        while (*fname == ' ') fname++;
        basic_load_and_run(fname);
    } else if (strncmp(cmd, "cc ", 3) == 0) {
        const char *args = cmd + 3;
        while (*args == ' ') args++;
        /* Check for -r flag */
        char fname[32];
        int fi = 0;
        while (args[fi] && args[fi] != ' ' && fi < 31) {
            fname[fi] = args[fi];
            fi++;
        }
        fname[fi] = '\0';
        /* Check if " -r" follows */
        const char *rest = args + fi;
        while (*rest == ' ') rest++;
        if (strcmp(rest, "-r") == 0) {
            cc_compile_and_run(fname);
        } else {
            cc_compile(fname);
        }
    } else if (strncmp(cmd, "run ", 4) == 0) {
        const char *fname = cmd + 4;
        while (*fname == ' ') fname++;
        if (*fname == '\0') {
            vga_write("Usage: run <filename.bin>\n");
        } else {
            prog_exec(fname);
        }
    } else if (strncmp(cmd, "debug ", 6) == 0) {
        const char *fname = cmd + 6;
        while (*fname == ' ') fname++;
        if (*fname == '\0') {
            vga_write("Usage: debug <filename.bin>\n");
        } else {
            debugger_run(fname);
        }
    } else if (strcmp(cmd, "sync") == 0) {
        if (diskfs_sync() == 0) {
            vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
            vga_write("Synced to disk\n");
            vga_set_color(VGA_WHITE, VGA_BLACK);
        } else {
            vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
            vga_write("No disk available\n");
            vga_set_color(VGA_WHITE, VGA_BLACK);
        }
    } else if (strncmp(cmd, "rm ", 3) == 0) {
        const char *filename = cmd + 3;
        while (*filename == ' ') filename++;
        if (*filename == '\0') {
            vga_write("Usage: rm <filename>\n");
        } else {
            if (diskfs_delete_file(filename) == 0) {
                vga_write("Deleted: ");
                vga_write(filename);
                vga_putchar('\n');
            } else {
                vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
                vga_write("File not found: ");
                vga_set_color(VGA_WHITE, VGA_BLACK);
                vga_write(filename);
                vga_putchar('\n');
            }
        }
    } else if (strcmp(cmd, "reboot") == 0) {
        vga_write("Rebooting...\n");
        serial_write("Rebooting...\n");
        /* Triple-fault: load null IDT, then trigger interrupt */
        struct { uint16_t limit; uint32_t base; } __attribute__((packed)) null_idt = {0, 0};
        __asm__ volatile("lidt %0; int $0x03" : : "m"(null_idt));
    } else if (strcmp(cmd, "shutdown") == 0) {
        vga_write("Shutting down...\n");
        serial_write("Shutting down...\n");
        /* QEMU i440FX/PIIX4 ACPI shutdown:
         * 1. Set PM base address in PIIX4 PM PCI config (bus 0, dev 1, func 3)
         * 2. Enable ACPI I/O space
         * 3. Write SLP_EN | SLP_TYP=S5 to PM1a control register */
        uint32_t pci_addr = 0x80000000 | (1 << 11) | (3 << 8);
        /* Set PMBA (offset 0x40) to 0xB000 */
        outl(0xCF8, pci_addr | 0x40);
        outl(0xCFC, 0xB001);
        /* Enable ACPI I/O: PMREGMISC (offset 0x80) bit 0 */
        outl(0xCF8, pci_addr | 0x80);
        outb(0xCFC, inb(0xCFC) | 0x01);
        /* PM1a_CNT = PMBA + 4, write SLP_EN (bit 13) | SLP_TYP for S5 */
        outw(0xB004, 0x2000);
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

/* Append a char to the command buffer and echo it (same as normal input). */
static void shell_emit(char c) {
    if (cmd_len < CMD_MAX_LEN) {
        cmd_buf[cmd_len++] = c;
        vga_putchar(c);
        serial_putchar(c);
    }
}

/* Tab completion: complete the current token against either the command list
   (first word) or the root directory file names (later arguments). */
static void shell_complete(void) {
    /* Find the start of the token under the cursor (after the last space). */
    uint32_t tok_start = cmd_len;
    while (tok_start > 0 && cmd_buf[tok_start - 1] != ' ') tok_start--;
    int is_cmd = (tok_start == 0);
    const char *prefix = cmd_buf + tok_start;
    uint32_t prefix_len = cmd_len - tok_start;

    struct fat12_dirent entries[FAT12_MAX_FILES];
    int file_count = 0;
    int cand_count;
    if (is_cmd) {
        cand_count = 0;
        while (shell_commands[cand_count]) cand_count++;
    } else {
        file_count = fat12_list_root(entries, FAT12_MAX_FILES);
        cand_count = file_count;
    }

    char lcp[16];           /* longest common prefix of all matches */
    uint32_t lcp_len = 0;
    int match_count = 0;

    for (int i = 0; i < cand_count; i++) {
        const char *name = is_cmd ? shell_commands[i] : entries[i].name;
        if (strncmp(name, prefix, prefix_len) != 0) continue;

        if (match_count == 0) {
            uint32_t n = strlen(name);
            if (n > sizeof(lcp) - 1) n = sizeof(lcp) - 1;
            for (uint32_t j = 0; j < n; j++) lcp[j] = name[j];
            lcp_len = n;
        } else {
            uint32_t j = 0;
            while (j < lcp_len && name[j] == lcp[j]) j++;
            lcp_len = j;
        }
        match_count++;
    }

    if (match_count == 0) return;

    /* Extend the buffer with the shared completion characters. */
    for (uint32_t j = prefix_len; j < lcp_len; j++) shell_emit(lcp[j]);

    if (match_count == 1) {
        shell_emit(' ');
    } else if (lcp_len == prefix_len) {
        /* Ambiguous and nothing to extend: list the candidates. */
        vga_putchar('\n');
        serial_putchar('\n');
        for (int i = 0; i < cand_count; i++) {
            const char *name = is_cmd ? shell_commands[i] : entries[i].name;
            if (strncmp(name, prefix, prefix_len) != 0) continue;
            vga_write(name);
            vga_write("  ");
        }
        vga_putchar('\n');
        shell_prompt();
        for (uint32_t j = 0; j < cmd_len; j++) {
            vga_putchar(cmd_buf[j]);
            serial_putchar(cmd_buf[j]);
        }
    }
}

static void shell_process_char(int c) {
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
        shell_complete();
    } else if (c >= ' ' && c < 127 && cmd_len < CMD_MAX_LEN) {
        cmd_buf[cmd_len++] = (char)c;
        vga_putchar((char)c);
        serial_putchar((char)c);
    }
}

void shell_run(void) {
    while (1) {
        int c = keyboard_getchar();
        shell_process_char(c);
    }
}
