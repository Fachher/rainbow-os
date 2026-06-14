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
/* graphics / input / timing (ring-3 games) */
#define SYS_TICKS       8
#define SYS_KEYDOWN     9
#define SYS_KEYDOWN_EXT 10
#define SYS_BLIT        11
#define SYS_GETFONT     12
#define SYS_YIELD       13
#define SYS_CLEAR       14
#define SYS_KBFLUSH     15
/* console cells / file I/O / argument (for the editor and other apps) */
#define SYS_PUTAT       16
#define SYS_SETCUR      17
#define SYS_DIMS        18
#define SYS_READFILE    19
#define SYS_WRITEFILE   20
#define SYS_GETARG      21
#define SYS_COUNT       22

/* Initialize syscall table */
void runtime_init(void);

/* Execute a flat binary from ramdisk */
void prog_exec(const char *filename);

/* Stash the command-line argument for the next program (read via SYS_GETARG). */
void prog_set_arg(const char *arg);

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
