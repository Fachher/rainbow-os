#include "cc/runtime.h"
#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "lib/string.h"
#include "fs/fat12.h"
#include "drivers/serial.h"
#include "cc/codegen.h"

/* Syscall implementations called by user programs */

static void sys_putchar(int c) {
    vga_putchar((char)c);
}

static int sys_getchar(void) {
    return keyboard_getchar();
}

static void sys_puts(const char *s) {
    vga_write(s);
}

static int sys_peek(int addr) {
    return *((volatile uint8_t *)(uint32_t)addr);
}

static void sys_poke(int addr, int val) {
    *((volatile uint8_t *)(uint32_t)addr) = (uint8_t)val;
}

static void sys_memset_wrap(void *dst, int val, int n) {
    memset(dst, val, (size_t)n);
}

/* Syscall table: array of function pointers at a fixed address */
typedef void (*syscall_fn)(void);

void runtime_init(void) {
    uint32_t *table = (uint32_t *)SYSCALL_TABLE_ADDR;
    table[SYS_PUTCHAR] = (uint32_t)sys_putchar;
    table[SYS_GETCHAR] = (uint32_t)sys_getchar;
    table[SYS_PUTS]    = (uint32_t)sys_puts;
    table[SYS_EXIT]    = 0;  /* filled per-execution with return address */
    table[SYS_PEEK]    = (uint32_t)sys_peek;
    table[SYS_POKE]    = (uint32_t)sys_poke;
    table[SYS_MEMSET]  = (uint32_t)sys_memset_wrap;
}

/* Return-to-shell flag, set by exit syscall */
static volatile int prog_exited;
static int prog_exit_code;

static void sys_exit(int code) {
    prog_exit_code = code;
    prog_exited = 1;
}

void prog_exec(const char *filename) {
    static uint8_t file_buf[CG_CODE_MAX + CG_STRING_MAX + CG_DATA_MAX];

    int bytes = fat12_read_file(filename, file_buf, sizeof(file_buf));
    if (bytes < 0) {
        vga_set_color(12, 0);
        vga_write("File not found: ");
        vga_set_color(15, 0);
        vga_write(filename);
        vga_putchar('\n');
        return;
    }

    serial_write("Executing: ");
    serial_write(filename);
    serial_write("\n");

    /* Copy to execution address */
    uint8_t *exec_addr = (uint8_t *)CG_LOAD_ADDR;
    memcpy(exec_addr, file_buf, bytes);

    /* Set up exit syscall */
    uint32_t *table = (uint32_t *)SYSCALL_TABLE_ADDR;
    table[SYS_EXIT] = (uint32_t)sys_exit;

    prog_exited = 0;
    prog_exit_code = 0;

    /* Call the program entry point.
       The program's first instruction is a JMP to main.
       main() returns to here via RET.
       If the program calls exit(), sys_exit sets the flag. */
    typedef int (*prog_entry_t)(void);
    prog_entry_t entry = (prog_entry_t)CG_LOAD_ADDR;

    int result = entry();

    if (!prog_exited) {
        prog_exit_code = result;
    }

    serial_write("Program exited with code ");
    serial_putchar('0' + (prog_exit_code % 10));
    serial_write("\n");
}
