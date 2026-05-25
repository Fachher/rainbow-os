/* crt0.h - Rainbow-OS C runtime
   Built-in functions are compiler intrinsics — no declarations needed.
   The compiler emits direct syscall table calls for:
   putchar(c), getchar(), puts(s), exit(code),
   peek(addr), poke(addr,val), memset(dst,val,n),
   printf(fmt, ...) */
