#include "basic/basic.h"
#include "basic/token.h"
#include "basic/program.h"
#include "basic/tokenizer.h"
#include "basic/expr.h"
#include "basic/vars.h"
#include "basic/exec.h"
#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "fs/fat12.h"
#include "fs/diskfs.h"
#include "lib/string.h"

#define INPUT_MAX 160

/* Forward declarations */
static void i32_to_str_basic(int32_t val, char *buf);
static void cmd_list(const char *args);
static void cmd_run(void);
static void cmd_save(const char *filename);
static void cmd_load(const char *filename);

/* Simple int-to-string for BASIC */
static void i32_to_str_basic(int32_t val, char *buf) {
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    int i = 0;
    bool neg = false;
    if (val < 0) { neg = true; val = -val; }
    char tmp[12];
    while (val > 0) { tmp[i++] = '0' + (char)(val % 10); val /= 10; }
    int p = 0;
    if (neg) buf[p++] = '-';
    while (i > 0) buf[p++] = tmp[--i];
    buf[p] = '\0';
}

/* Read a line from keyboard with echo */
static uint8_t basic_readline(char *buf, uint8_t max) {
    uint8_t len = 0;
    while (1) {
        int c = keyboard_getchar();
        if (c == '\n') {
            vga_putchar('\n');
            buf[len] = '\0';
            return len;
        } else if (c == '\b') {
            if (len > 0) {
                len--;
                vga_putchar('\b');
            }
        } else if (c == KEY_CTRL('c')) {
            vga_putchar('\n');
            buf[0] = '\0';
            return 0;
        } else if (c >= ' ' && c < 127 && len < max - 1) {
            buf[len++] = (char)c;
            vga_putchar((char)c);
        }
    }
}

/* Convert character to uppercase */
static char to_upper(char c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    return c;
}

/* Parse line number from start of string. Returns 0 if none. */
static uint16_t parse_line_number(const char *s, int *advance) {
    uint16_t num = 0;
    int i = 0;
    while (s[i] >= '0' && s[i] <= '9') {
        num = num * 10 + (uint16_t)(s[i] - '0');
        i++;
    }
    *advance = i;
    return (i > 0) ? num : 0;
}

/* Case-insensitive command match */
static bool cmd_match(const char *input, const char *cmd) {
    while (*cmd) {
        if (to_upper(*input) != *cmd) return false;
        input++;
        cmd++;
    }
    return (*input == '\0' || *input == ' ');
}

/* LIST command */
static void cmd_list(const char *args) {
    uint16_t from_line = 0, to_line = 9999;

    /* Parse optional range: LIST 10-50 */
    if (args && *args) {
        while (*args == ' ') args++;
        int adv;
        from_line = parse_line_number(args, &adv);
        if (adv > 0) {
            args += adv;
            if (*args == '-') {
                args++;
                to_line = parse_line_number(args, &adv);
                if (adv == 0) to_line = 9999;
            } else {
                to_line = from_line;
            }
        } else {
            from_line = 0;
            to_line = 9999;
        }
    }

    uint8_t *line = prog_first_line();
    char detok_buf[160];
    char num_buf[8];
    while (line) {
        uint16_t num = prog_line_number(line);
        if (num >= from_line && num <= to_line) {
            /* Print line number */
            i32_to_str_basic(num, num_buf);
            vga_write(num_buf);
            vga_putchar(' ');

            /* Detokenize and print */
            uint8_t data_len = prog_line_length(line) - 3;
            detokenize(line + 3, data_len, detok_buf, sizeof(detok_buf));
            vga_write(detok_buf);
            vga_putchar('\n');
        }
        line = prog_next_line(line);
    }
}

/* RUN command */
static void cmd_run(void) {
    exec_init(&basic_state);
    exec_clear_error();
    vars_clear();
    string_pool_reset();

    basic_state.current_line = prog_first_line();
    basic_state.running = true;
    uint8_t resume_pos = 0;

    while (basic_state.running && basic_state.current_line && !exec_has_error()) {
        uint8_t *line = basic_state.current_line;
        uint8_t data_len = prog_line_length(line) - 3;
        basic_state.jumped = false;

        exec_line_at(&basic_state, line + 3, data_len, resume_pos);
        resume_pos = 0;  /* default: start from beginning of next line */

        if (basic_state.running && !exec_has_error()) {
            if (basic_state.jumped) {
                /* GOTO/GOSUB/NEXT changed current_line; pos may be mid-line for NEXT */
                resume_pos = basic_state.pos;
            } else {
                basic_state.current_line = prog_next_line(basic_state.current_line);
            }
        }
    }

    if (exec_has_error()) {
        vga_write("?");
        vga_write(exec_error_msg());
        uint16_t eline = exec_error_line();
        if (eline > 0) {
            vga_write(" IN ");
            char nbuf[8];
            i32_to_str_basic(eline, nbuf);
            vga_write(nbuf);
        }
        vga_putchar('\n');
    }
}

/* SAVE command */
static void cmd_save(const char *filename) {
    if (!filename || !*filename) {
        vga_write("?MISSING FILENAME\n");
        return;
    }

    static uint8_t save_buf[4096];
    uint32_t offset = 0;
    char detok_buf[160];
    char num_buf[8];

    uint8_t *line = prog_first_line();
    while (line && offset < sizeof(save_buf) - 170) {
        uint16_t num = prog_line_number(line);
        i32_to_str_basic(num, num_buf);

        /* Copy line number */
        for (int i = 0; num_buf[i]; i++)
            save_buf[offset++] = (uint8_t)num_buf[i];
        save_buf[offset++] = ' ';

        /* Detokenize */
        uint8_t data_len = prog_line_length(line) - 3;
        detokenize(line + 3, data_len, detok_buf, sizeof(detok_buf));
        for (int i = 0; detok_buf[i]; i++)
            save_buf[offset++] = (uint8_t)detok_buf[i];
        save_buf[offset++] = '\n';

        line = prog_next_line(line);
    }

    if (diskfs_write_file(filename, save_buf, offset) == 0) {
        vga_write("OK\n");
    } else {
        vga_write("?SAVE ERROR\n");
    }
}

/* LOAD command */
static void cmd_load(const char *filename) {
    if (!filename || !*filename) {
        vga_write("?MISSING FILENAME\n");
        return;
    }

    static uint8_t load_buf[4096];
    int bytes = fat12_read_file(filename, load_buf, sizeof(load_buf));
    if (bytes < 0) {
        vga_write("?FILE NOT FOUND\n");
        return;
    }

    prog_init();

    /* Parse each line from the file */
    int i = 0;
    char line_buf[160];
    while (i < bytes) {
        int li = 0;
        while (i < bytes && load_buf[i] != '\n' && li < 158) {
            line_buf[li++] = (char)load_buf[i++];
        }
        line_buf[li] = '\0';
        if (i < bytes && load_buf[i] == '\n') i++;

        if (li == 0) continue;

        /* Parse line number */
        int adv;
        uint16_t num = parse_line_number(line_buf, &adv);
        if (num == 0) continue;

        /* Skip space after line number */
        int si = adv;
        while (line_buf[si] == ' ') si++;

        /* Tokenize and insert */
        uint8_t tok_buf[120];
        uint8_t tok_len = tokenize(line_buf + si, tok_buf, sizeof(tok_buf));
        if (tok_len > 0) {
            prog_insert_line(num, tok_buf, tok_len);
        }
    }

    vga_write("OK\n");
}

/* Main BASIC REPL */
void basic_run(void) {
    prog_init();
    vars_clear();
    exec_init(&basic_state);
    exec_clear_error();

    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_write("Rainbow BASIC v1.0\n");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_write("8192 BYTES FREE\n");
    vga_write("READY.\n");

    char input[INPUT_MAX];

    while (1) {
        basic_readline(input, INPUT_MAX);

        if (input[0] == '\0') continue;

        /* Check for SYSTEM command to exit */
        if (cmd_match(input, "SYSTEM")) break;

        /* Check if line starts with a number */
        int adv;
        uint16_t line_num = parse_line_number(input, &adv);

        if (line_num > 0) {
            /* Skip spaces after line number */
            int si = adv;
            while (input[si] == ' ') si++;

            if (input[si] == '\0') {
                /* Just a line number = delete that line */
                prog_delete_line(line_num);
            } else {
                /* Tokenize and store */
                uint8_t tok_buf[120];
                uint8_t tok_len = tokenize(input + si, tok_buf, sizeof(tok_buf));
                if (tok_len > 0) {
                    prog_insert_line(line_num, tok_buf, tok_len);
                }
            }
            continue;
        }

        /* Immediate mode commands */
        if (cmd_match(input, "RUN")) {
            cmd_run();
            vga_write("READY.\n");
            continue;
        }
        if (cmd_match(input, "LIST")) {
            const char *args = input + 4;
            while (*args == ' ') args++;
            cmd_list(args);
            continue;
        }
        if (cmd_match(input, "NEW")) {
            prog_init();
            vars_clear();
            vga_write("READY.\n");
            continue;
        }
        if (cmd_match(input, "SAVE")) {
            const char *args = input + 4;
            while (*args == ' ') args++;
            /* Strip quotes */
            if (*args == '"') {
                args++;
                char fname[13];
                int fi = 0;
                while (args[fi] && args[fi] != '"' && fi < 12)  {
                    fname[fi] = args[fi];
                    fi++;
                }
                fname[fi] = '\0';
                cmd_save(fname);
            } else {
                cmd_save(args);
            }
            continue;
        }
        if (cmd_match(input, "LOAD")) {
            const char *args = input + 4;
            while (*args == ' ') args++;
            if (*args == '"') {
                args++;
                char fname[13];
                int fi = 0;
                while (args[fi] && args[fi] != '"' && fi < 12) {
                    fname[fi] = args[fi];
                    fi++;
                }
                fname[fi] = '\0';
                cmd_load(fname);
            } else {
                cmd_load(args);
            }
            continue;
        }

        /* Try as immediate-mode statement */
        uint8_t tok_buf[120];
        uint8_t tok_len = tokenize(input, tok_buf, sizeof(tok_buf));
        if (tok_len > 0) {
            exec_immediate(tok_buf, tok_len);
            if (exec_has_error()) {
                vga_write("?");
                vga_write(exec_error_msg());
                vga_putchar('\n');
                exec_clear_error();
            }
        }
    }
}

/* Load and run from OS shell */
void basic_load_and_run(const char *filename) {
    prog_init();
    vars_clear();
    exec_init(&basic_state);
    exec_clear_error();

    cmd_load(filename);
    if (prog_first_line()) {
        cmd_run();
    }
}
