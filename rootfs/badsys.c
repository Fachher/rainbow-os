/* Syscall escalation test: ask the kernel to write to kernel memory via poke().
   With pointer validation, this is killed; without it, poke succeeds (escalation).
   The "FAILED" line must never print. */
int main() {
    poke(0x100000, 65);
    printf("ISOLATION FAILED - kernel poke succeeded\n");
    return 0;
}
