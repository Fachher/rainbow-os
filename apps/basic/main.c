/* Entry point for BASIC as a ring-3 user program.
   `run basic.bin`           -> interactive REPL
   `run basic.bin prog.bas`  -> load and run a program. */

#include "basic/basic.h"
#include "syscall.h"

int main(void) {
    static char arg[16];
    sys_getarg(arg, sizeof(arg));
    if (arg[0]) basic_load_and_run(arg);
    else        basic_run();
    return 0;
}
