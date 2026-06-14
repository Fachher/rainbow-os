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
#define SYS_PRINTF      7
#define SYS_COUNT        8

/* Initialize syscall table */
void runtime_init(void);

/* Execute a flat binary from ramdisk */
void prog_exec(const char *filename);

/* Load a flat binary to CG_LOAD_ADDR, install the exit stub and seed the user
   stack. Returns the initial user ESP, or 0 on failure (e.g. file not found).
   Shared by prog_exec and the debugger. */
uint32_t prog_load(const char *filename);

/* Ring 3 entry / exit (kernel/usermode.asm) and the fault-kill path
   (cc/runtime.c) — used by the debugger to run and stop a program. */
void enter_user(uint32_t entry_eip, uint32_t user_esp);
void return_to_kernel(void);
void prog_fault(const char *reason);

#endif
