#include "cc/runtime.h"
#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "lib/string.h"
#include "fs/fat12.h"
#include "drivers/serial.h"
#include "cc/codegen.h"
#include "include/idt.h"
#include "include/paging.h"

/* Ring 3 entry/exit (kernel/usermode.asm). saved_kernel_esp must be a global
   (the asm references it). */
uint32_t saved_kernel_esp;
extern void enter_user(uint32_t entry_eip, uint32_t user_esp);
extern void return_to_kernel(void);

/* Address of a tiny user-mode exit stub written into the user region, used as
   the return address for the program's main(). */
#define USER_EXIT_STUB 0x2F0000

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

static void print_dec(int val) {
    if (val < 0) {
        vga_putchar('-');
        val = -val;
    }
    char buf[12];
    int i = 0;
    if (val == 0) {
        buf[i++] = '0';
    } else {
        while (val > 0) {
            buf[i++] = '0' + val % 10;
            val /= 10;
        }
    }
    while (--i >= 0) vga_putchar(buf[i]);
}

static void print_unsigned(uint32_t val) {
    char buf[12];
    int i = 0;
    if (val == 0) {
        buf[i++] = '0';
    } else {
        while (val > 0) {
            buf[i++] = '0' + val % 10;
            val /= 10;
        }
    }
    while (--i >= 0) vga_putchar(buf[i]);
}

static void print_hex(uint32_t val) {
    char buf[9];
    int i = 0;
    if (val == 0) {
        buf[i++] = '0';
    } else {
        while (val > 0) {
            int d = val & 0xF;
            buf[i++] = d < 10 ? '0' + d : 'a' + d - 10;
            val >>= 4;
        }
    }
    while (--i >= 0) vga_putchar(buf[i]);
}

static int do_printf(const char *fmt, uint32_t *ap) {
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
                case 'd': print_dec((int)*ap++); break;
                case 'u': print_unsigned(*ap++); break;
                case 'x': print_hex(*ap++); break;
                case 's': {
                    const char *s = (const char *)*ap++;
                    if (s) vga_write(s);
                    break;
                }
                case 'c': vga_putchar((char)*ap++); break;
                case '%': vga_putchar('%'); break;
                default:
                    vga_putchar('%');
                    vga_putchar(*fmt);
                    break;
            }
        } else {
            vga_putchar(*fmt);
        }
        fmt++;
    }
    return 0;
}

/* Return-to-shell flag, set by exit syscall */
static volatile int prog_exited;
static int prog_exit_code;

static void sys_exit(int code) {
    prog_exit_code = code;
    prog_exited = 1;
    return_to_kernel();          /* does not return */
}

/* Called by the GP / page-fault handlers when a ring-3 program faults. */
void prog_fault(const char *reason) {
    vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
    vga_write("\n[program killed: ");
    vga_write(reason);
    vga_write("]\n");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    prog_exit_code = -1;
    prog_exited = 1;
    return_to_kernel();          /* does not return */
}

/* #GP handler: a ring-3 program executing a privileged instruction or touching
   a forbidden selector traps here. Kill the program; halt only for kernel #GP. */
static void gpf_handler(struct isr_frame *f) {
    if ((f->cs & 3) == 3) {
        prog_fault("protection fault");
    }
    serial_write("KERNEL #GP eip=0x");
    serial_write_hex(f->eip);
    serial_write("\n");
    __asm__ volatile("cli; hlt");
}

/* int 0x80 dispatcher.
   ABI: eax = syscall number, ebx = pointer to the caller's argument block
   (cdecl args on the program's stack). Return value goes back in eax. */
static void syscall_handler(struct isr_frame *f) {
    uint32_t *a = (uint32_t *)f->ebx;
    switch (f->eax) {
        case SYS_PUTCHAR: sys_putchar((int)a[0]);                       break;
        case SYS_GETCHAR: f->eax = (uint32_t)sys_getchar();             break;
        case SYS_PUTS:    sys_puts((const char *)a[0]);                 break;
        case SYS_EXIT:    sys_exit((int)a[0]);                          break;
        case SYS_PEEK:    f->eax = (uint32_t)sys_peek((int)a[0]);       break;
        case SYS_POKE:    sys_poke((int)a[0], (int)a[1]);               break;
        case SYS_MEMSET:  sys_memset_wrap((void *)a[0], (int)a[1], (int)a[2]); break;
        case SYS_PRINTF:  f->eax = (uint32_t)do_printf((const char *)a[0], &a[1]); break;
        default: break;
    }
}

void runtime_init(void) {
    register_interrupt_handler(128, syscall_handler);
    register_interrupt_handler(13, gpf_handler);   /* general protection fault */
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

    /* Exit stub in user memory: push 0; mov ebx,esp; mov eax,SYS_EXIT; int 0x80.
       main()'s RET lands here when the program returns instead of calling exit(). */
    uint8_t *stub = (uint8_t *)USER_EXIT_STUB;
    static const uint8_t stub_code[] = {
        0x6A, 0x00,                         /* push 0            */
        0x89, 0xE3,                         /* mov ebx, esp      */
        0xB8, SYS_EXIT, 0x00, 0x00, 0x00,   /* mov eax, SYS_EXIT */
        0xCD, 0x80,                         /* int 0x80          */
    };
    memcpy(stub, stub_code, sizeof(stub_code));

    /* User stack: top dword = return address (the exit stub) for main()'s RET. */
    uint32_t *ustack = (uint32_t *)(USER_STACK_TOP - 4);
    *ustack = USER_EXIT_STUB;

    prog_exited = 0;
    prog_exit_code = 0;

    /* Drop to ring 3 at the program entry. Returns here (via return_to_kernel)
       when the program calls exit(), returns from main, or faults. */
    enter_user(CG_LOAD_ADDR, (uint32_t)ustack);

    serial_write("Program exited with code ");
    serial_putchar('0' + (prog_exit_code % 10));
    serial_write("\n");
}
