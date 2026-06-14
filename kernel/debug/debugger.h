#ifndef DEBUGGER_H
#define DEBUGGER_H

/* Load a cc-compiled flat binary and run it under the interactive debugger.
   Returns to the shell when the program exits or the user quits. */
void debugger_run(const char *filename);

#endif
