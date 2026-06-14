; Userland syscall stubs. ABI: ebx = &args (cdecl args on the stack),
; eax = syscall number, int 0x80; return value in eax.

[BITS 32]
section .text

%macro SYSCALL 2
global %1
%1:
    lea ebx, [esp + 4]      ; point at the first cdecl argument
    mov eax, %2
    int 0x80
    ret
%endmacro

SYSCALL sys_putchar,     0
SYSCALL sys_getchar,     1
SYSCALL sys_puts,        2
SYSCALL sys_exit,        3
SYSCALL sys_printf,      7
SYSCALL sys_ticks,       8
SYSCALL sys_keydown,     9
SYSCALL sys_keydown_ext, 10
SYSCALL sys_blit,        11
SYSCALL sys_getfont,     12
SYSCALL sys_yield,       13
SYSCALL sys_clear,       14
SYSCALL sys_kbflush,     15
SYSCALL sys_putat,       16
SYSCALL sys_setcur,      17
SYSCALL sys_dims,        18
SYSCALL sys_readfile,    19
SYSCALL sys_writefile,   20
SYSCALL sys_getarg,      21
