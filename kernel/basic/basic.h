#ifndef BASIC_BASIC_H
#define BASIC_BASIC_H

/* Enter BASIC interactive mode (REPL). Returns when user types SYSTEM. */
void basic_run(void);

/* Load a .bas file and run it immediately */
void basic_load_and_run(const char *filename);

#endif
