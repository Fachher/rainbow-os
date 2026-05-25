#include "cc/cc.h"
#include "cc/lexer.h"
#include "cc/preproc.h"
#include "cc/sym.h"
#include "cc/parser.h"
#include "cc/codegen.h"
#include "cc/runtime.h"
#include "fs/fat12.h"
#include "fs/diskfs.h"
#include "drivers/vga.h"
#include "drivers/serial.h"
#include "lib/string.h"
#include "include/types.h"

#define SRC_BUF_SIZE   8192
#define PP_BUF_SIZE    8192

static uint8_t src_buf[SRC_BUF_SIZE];
static char pp_buf[PP_BUF_SIZE];

/* Build output filename: replace .c with .bin, or append .bin */
static void make_output_name(const char *input, char *output, int max) {
    int len = strlen(input);
    int i;
    for (i = 0; i < len && i < max - 5; i++)
        output[i] = input[i];

    /* Find last '.' */
    int dot = -1;
    for (int j = i - 1; j >= 0; j--) {
        if (output[j] == '.') { dot = j; break; }
    }

    if (dot >= 0) {
        output[dot] = '.';
        output[dot+1] = 'b';
        output[dot+2] = 'i';
        output[dot+3] = 'n';
        output[dot+4] = '\0';
    } else {
        output[i] = '.';
        output[i+1] = 'b';
        output[i+2] = 'i';
        output[i+3] = 'n';
        output[i+4] = '\0';
    }
}

int cc_compile(const char *filename) {
    /* Read source file */
    int bytes = fat12_read_file(filename, src_buf, SRC_BUF_SIZE - 1);
    if (bytes < 0) {
        vga_set_color(12, 0);
        vga_write("File not found: ");
        vga_set_color(15, 0);
        vga_write(filename);
        vga_putchar('\n');
        return -1;
    }
    src_buf[bytes] = '\0';

    serial_write("CC: compiling ");
    serial_write(filename);
    serial_write("\n");

    /* Preprocess */
    int pp_len = preprocess((const char *)src_buf, bytes, pp_buf, PP_BUF_SIZE);
    if (pp_len < 0) {
        vga_set_color(12, 0);
        vga_write("Preprocessor error\n");
        vga_set_color(15, 0);
        return -1;
    }

    /* Initialize subsystems */
    sym_init();
    cg_init();
    lex_init(pp_buf, pp_len);

    /* Parse and generate code */
    parse_program();

    if (parse_had_error()) {
        return -1;
    }

    /* Get output binary */
    int out_size;
    uint8_t *out = cg_output(&out_size);

    if (out_size <= 0) {
        vga_set_color(12, 0);
        vga_write("Compilation failed\n");
        vga_set_color(15, 0);
        return -1;
    }

    /* Write output file */
    char out_name[32];
    make_output_name(filename, out_name, sizeof(out_name));

    if (diskfs_write_file(out_name, out, out_size) < 0) {
        vga_set_color(12, 0);
        vga_write("Failed to write ");
        vga_write(out_name);
        vga_putchar('\n');
        vga_set_color(15, 0);
        return -1;
    }

    vga_set_color(10, 0); /* green */
    vga_write("Compiled ");
    vga_write(filename);
    vga_write(" -> ");
    vga_write(out_name);
    vga_write(" (");
    vga_write_dec(out_size);
    vga_write(" bytes)\n");
    vga_set_color(15, 0);

    return 0;
}

void cc_compile_and_run(const char *filename) {
    if (cc_compile(filename) < 0) return;

    char out_name[32];
    make_output_name(filename, out_name, sizeof(out_name));

    prog_exec(out_name);
}
