/* crt0.h - Rainbow-OS C runtime */
#define SYSCALL_TABLE 0x1F0000

void putchar(int c) {
    int *table;
    table = (int *)SYSCALL_TABLE;
    int fn;
    fn = table[0];
    /* Call via inline pointer call */
    int *fptr;
    fptr = (int *)fn;
    /* We use peek/poke style: write char to VGA directly */
}

void puts(char *s) {
    while (*s) {
        putchar(*s);
        s = s + 1;
    }
}
