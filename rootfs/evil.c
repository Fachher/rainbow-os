/* Isolation test: try to write directly to kernel memory (0x100000).
   In Ring 3 this store hits a supervisor-only page and must fault, so the
   "FAILED" line should never print. */
int main() {
    char *p = (char *)0x100000;
    *p = 65;
    printf("ISOLATION FAILED - wrote to kernel memory\n");
    return 0;
}
