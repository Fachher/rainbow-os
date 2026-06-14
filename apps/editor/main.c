/* Entry point for the editor as a ring-3 user program.
   Reads the filename argument (run editor.bin <file>) and opens the editor. */

#include "editor.h"
#include "syscall.h"

int main(void) {
    static char arg[16];
    sys_getarg(arg, sizeof(arg));
    editor_open(arg[0] ? arg : 0);
    return 0;
}
