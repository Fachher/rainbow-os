# Rainbow-OS Debugger

An in-OS, machine-level debugger for `cc`-compiled programs. It runs a program in
Ring 3 under your control: set breakpoints, single-step, inspect registers and
memory, disassemble, and walk the call stack — all from the Rainbow-OS shell.

It is **machine-level**: addresses, registers and raw bytes, not C source lines or
variable names (the `cc` compiler emits no debug symbols).

## Quick start

```
> cc dbgtest.c          # produces dbgtest.bin
> debug dbgtest.bin     # stops at the first instruction, opens the (dbg) prompt
```

`debug <file.bin>` takes a compiled flat binary, so compile first with `cc`. The
program halts at its entry point (`0x200000`) and drops you into the debugger
REPL:

```
[debugger] dbgtest.bin loaded. h=help, c=continue.

-- stopped @ 0x00200000 --
00200000:  jmp  0x00200039
00200005:  push ebp
...
(dbg)
```

Type `c` to run, or any command below. When the program exits (or you `q`uit),
you return to the shell.

## Commands

| Command      | Action |
|--------------|--------|
| `r`          | Show registers (eax…edi, eip, esp, eflags with `[TSZC]` flags) |
| `x ADDR [N]` | Examine memory: hex + ASCII dump of `N` bytes (default 64) |
| `u [ADDR]`   | Disassemble ~10 instructions (default: at current EIP) |
| `bt`         | Backtrace — walk the `ebp` call-frame chain |
| `b ADDR`     | Set a breakpoint at `ADDR` |
| `d ADDR`     | Delete the breakpoint at `ADDR` |
| `s`          | Single-step one instruction |
| `c`          | Continue until the next breakpoint or exit |
| `q`          | Quit — kill the program and return to the shell |
| `h`          | Help |

Addresses are hex, with or without `0x` (e.g. `b 200039` or `b 0x200039`).
Programs always load at `0x200000`, and the user region is `0x200000–0x300000`;
memory/breakpoint commands are restricted to that window.

## A worked example

Using `rootfs/dbgtest.c`:

```c
int add(int a, int b) {
    int s = a + b;
    return s;
}
int main() {
    int x = 7, y = 5;
    int z = add(x, y);
    printf("z = %d\n", z);
    return 0;
}
```

```
> cc dbgtest.c
> debug dbgtest.bin

-- stopped @ 0x00200000 --      ← entry: `jmp main`
(dbg) s                          ← step into main
-- stopped @ 0x00200039 --
(dbg) u                          ← see main's prologue + locals
00200039:  push ebp
0020003a:  mov  ebp, esp
0020003c:  sub  esp, 0x00000080
...
00200045:  mov  eax, 0x00000007  ← x = 7
0020004a:  mov  [ebp-4], eax
(dbg) b 0x20001f                 ← breakpoint on `add eax, ecx` inside add()
  bp set @ 0x0020001f
(dbg) c
-- stopped @ 0x0020001f --
(dbg) r                          ← inspect the operands in eax/ecx
EAX=00000005  EBX=00000000  ECX=00000007  EDX=...
(dbg) bt                         ← who called us
#0  0x0020001f                   ← add()
#1  0x0020006e                   ← return into main()
#2  0x002f0000                   ← the exit stub (main's return address)
(dbg) c                          ← finish
z = 12
[debugger] program finished
>
```

## How it works (brief)

- **Breakpoints** patch a `0xCC` (int3) byte at the target address. Hitting it
  raises `#BP`, which traps into the debugger. The original byte is restored while
  you're stopped and re-armed when you continue.
- **Single-step** uses the x86 EFLAGS Trap Flag (TF): after each instruction the
  CPU raises `#DB`, re-entering the debugger.
- The debugger runs **inside those trap handlers** (Ring 0). It reads the paused
  program's state from the saved interrupt frame and resumes by `iret` when you
  `c`ontinue or `s`tep.
- Stepping **over a syscall** (e.g. `printf`'s `int 0x80`) is automatic — the
  debugger doesn't descend into the kernel; execution resumes after the syscall.
- The disassembler understands the instruction subset the `cc` compiler emits;
  anything else prints as `db 0xNN`.

## Tips & limitations

- Compile first (`cc foo.c`) — `debug` runs the resulting `.bin`.
- Set breakpoints by address; use `u` to find the address of the instruction you
  want, then `b <addr>`.
- Up to 16 breakpoints at once.
- A program killed by a fault (e.g. a bad memory access) or by `q` returns you to
  a clean shell.
- **Machine-level only.** No source-line stepping or variable names yet — that
  needs the compiler to emit a symbol/line table (a planned follow-on).
- Single shared address space: one program is debugged at a time.

## Related

- `docs/ring3-plan.md` — the Ring 3 userland + syscall design the debugger builds on.
- Source: `kernel/debug/debugger.c` (REPL + trap handlers), `kernel/debug/disasm.c`
  (cc-subset disassembler).
