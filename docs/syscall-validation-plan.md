# Milestone M15: Syscall Pointer Validation

## Context
M14 put `run`/`cc -r` programs in Ring 3, so a user program can no longer touch
kernel memory *directly* (the page-fault test in `rootfs/evil.c` proves it). But
the **syscall interface is now the hole**: the kernel runs each syscall in Ring 0
on behalf of the user and currently trusts every pointer the user passes. A
malicious Ring 3 program can use the kernel as a confused deputy to read or write
arbitrary kernel memory — fully defeating the isolation M14 added.

Concrete holes in the dispatcher (`syscall_handler`, `kernel/cc/runtime.c:159`)
and the `sys_*` helpers:

| Syscall | Hole |
|---------|------|
| dispatcher | `uint32_t *a = (uint32_t *)f->ebx;` reads up to 3 dwords from a **user-controlled** `ebx` — a program can point `ebx` at kernel memory. |
| `sys_poke(addr,val)` (`runtime.c:39`) | `*(uint8_t*)addr = val` with attacker `addr` → **arbitrary kernel write** (full privilege escalation). |
| `sys_peek(addr)` (`runtime.c:35`) | `*(uint8_t*)addr` → **arbitrary kernel read** (info leak). |
| `sys_memset_wrap(dst,val,n)` (`runtime.c:43`) | `memset(dst,val,n)` with attacker `dst`/`n` → **arbitrary kernel write**. |
| `sys_puts(s)` (`runtime.c:31`) | walks a user string pointer; if it points at kernel memory → leak, and if it runs off into unmapped memory the **kernel** faults (ring-0 #PF → halt = DoS). |
| `do_printf(fmt, ap)` (`runtime.c:~82`) | reads `fmt` and varargs (`ap`) from user pointers; `%s` dereferences a user-supplied pointer → same leak / kernel-fault risk. |

Goal: validate every pointer and buffer length a syscall receives, and **kill the
program** (not the kernel) when validation fails — reusing the M14 `prog_fault()`
path.

## Design

### 1. Validation helpers — `kernel/cc/runtime.c`
Use the existing user-window bounds from `kernel/include/paging.h`
(`USER_REGION_BASE = 0x200000`, `USER_REGION_TOP = 0x300000`).

```c
/* True iff [addr, addr+len) lies entirely inside the ring-3 user window. */
static int user_range_ok(uint32_t addr, uint32_t len) {
    uint32_t end = addr + len;
    return len == 0 ||
           (end >= addr &&                       /* no wraparound */
            addr >= USER_REGION_BASE && end <= USER_REGION_TOP);
}

/* True iff a NUL-terminated string starting at addr stays inside the window.
   Bounds the scan at USER_REGION_TOP so a missing terminator can't loop. */
static int user_str_ok(uint32_t addr) {
    if (addr < USER_REGION_BASE || addr >= USER_REGION_TOP) return 0;
    for (uint32_t p = addr; p < USER_REGION_TOP; p++)
        if (*(const char *)p == '\0') return 1;
    return 0;
}
```

### 2. Validate the argument block before reading it — `syscall_handler`
`f->ebx` is attacker-controlled, so check it covers the dwords each syscall reads
*before* dereferencing `a[]`. Drive it from a per-syscall arg count:

```c
static const uint8_t sys_argc[SYS_COUNT] = {
    [SYS_PUTCHAR]=1, [SYS_GETCHAR]=0, [SYS_PUTS]=1, [SYS_EXIT]=1,
    [SYS_PEEK]=1, [SYS_POKE]=2, [SYS_MEMSET]=3, [SYS_PRINTF]=1, /* fmt; varargs checked in do_printf */
};
```

At the top of `syscall_handler`:
```c
if (f->eax >= SYS_COUNT || !user_range_ok(f->ebx, sys_argc[f->eax] * 4))
    prog_fault("bad syscall args");      /* does not return */
uint32_t *a = (uint32_t *)f->ebx;
```

### 3. Validate pointer/buffer args inside each syscall
- **`sys_poke` / `sys_peek`**: require `user_range_ok(addr, 1)`. **Policy note:**
  this restricts peek/poke to the program's own region, which makes them nearly
  useless (a program can already read/write its own memory directly). Recommended:
  keep the restriction (closes the escalation); optionally, in a later step,
  replace raw peek/poke with purpose-built syscalls (e.g. a framebuffer-draw
  syscall) and drop peek/poke from the user ABI entirely. Either way they must not
  remain "write anywhere".
- **`sys_memset_wrap`**: require `user_range_ok((uint32_t)dst, (uint32_t)n)` (and
  reject negative `n`).
- **`sys_puts`**: require `user_str_ok((uint32_t)s)`.
- **`do_printf`**: require `user_str_ok(fmt)`; while walking, bound `ap` reads with
  `user_range_ok((uint32_t)ap, 4)` before each `*ap++`, and for `%s` require
  `user_str_ok(*ap)` before printing the pointed string.

On any failure the syscall calls `prog_fault("bad syscall pointer")`. Since
`prog_fault` never returns (it does `return_to_kernel`), make the helpers return a
status the dispatcher checks, or call `prog_fault` directly from within them — the
latter is simpler and matches the existing fault path. Note all these run in
Ring 0 on the TSS `esp0` stack, so the coroutine swap back to `prog_exec` works
exactly as for the M14 fault/exit cases.

### 4. Keep the kernel side fault-safe
Even with validation, defense-in-depth: a kernel #PF that occurs *inside* a
syscall handler (i.e. `frame->cs` is ring 0) currently halts. That stays — but
because we now validate first, a well-behaved-looking-but-malicious program can no
longer trip it. (A future hardening step could make the page-fault handler treat
"#PF while servicing a syscall" as a program kill too, via an `in_syscall` flag.)

## Files touched
- `kernel/cc/runtime.c` — add `user_range_ok` / `user_str_ok`, the `sys_argc`
  table, the dispatcher precheck, per-syscall pointer checks in
  `sys_peek/sys_poke/sys_memset_wrap/sys_puts`, and bounds in `do_printf`.
- No new files; no changes to the compiler, paging, or GDT.

## Verification
Build and test headless in QEMU (`-monitor stdio` + `sendkey`/`screendump`,
PPM→PNG via PIL), the same workflow used for M14:
1. **Regression:** `cc printf.c -r` and `cc hello.c -r` still run and print
   correctly (legitimate pointers all lie in the user window).
2. **Write escalation blocked:** a test program calling `poke(0x100000, 0x41)`
   (kernel memory) is killed with `[program killed: bad syscall pointer]`, and the
   value at `0x100000` is unchanged (the kernel keeps running — `ls`/`version`
   work afterward). Compare against today's behavior where the poke would succeed.
3. **Read leak blocked:** `peek(0x100000)` / `printf("%s", (char*)0x100000)` are
   killed rather than dumping kernel bytes.
4. **DoS blocked:** `puts((char*)0x2FFFFF)` (a string with no terminator before the
   region end) is killed instead of faulting the kernel into a halt.
5. Add a `rootfs/badsys.c` covering the above (mirrors `rootfs/evil.c`); remember
   to delete `build/rainbow-os-hdd.img` so the persistent FAT12 partition is
   regenerated with the new file.
6. Confirm `kernel.bin` stays under the 128 KB size check.

## Notes / scope
- This closes the confused-deputy class of bugs for the current single-address-
  space model. It does **not** add per-process address spaces — that (separate
  page directories per program, so the user window itself is private) remains a
  later milestone and would also let `user_range_ok` be replaced by a proper
  "is this mapped & user-owned" check via the page tables.
