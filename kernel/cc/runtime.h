#ifndef CC_RUNTIME_H
#define CC_RUNTIME_H

#include "include/types.h"

/* Syscall jump table address — programs call kernel services through this */
#define SYSCALL_TABLE_ADDR  0x1F0000

/* Syscall indices */
#define SYS_PUTCHAR     0
#define SYS_GETCHAR     1
#define SYS_PUTS        2
#define SYS_EXIT        3
#define SYS_PEEK        4
#define SYS_POKE        5
#define SYS_MEMSET      6
#define SYS_COUNT        7

/* Initialize syscall table */
void runtime_init(void);

/* Execute a flat binary from ramdisk */
void prog_exec(const char *filename);

#endif
