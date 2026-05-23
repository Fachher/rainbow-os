#ifndef CC_CC_H
#define CC_CC_H

/* Compile a C file from ramdisk. Output is written as .bin file. */
int cc_compile(const char *filename);

/* Compile and immediately run */
void cc_compile_and_run(const char *filename);

#endif
