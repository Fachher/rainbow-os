# Milestone M14: Ring 3 (Userland) + Syscall Interface

## Context
Rainbow-OS currently has **no separation between userland and kernel mode** —
everything runs in Ring 0, in a single flat address space:

- The GDT (`boot/stage2.asm`) defines only Ring 0 code (`0x08`) and data (`0x10`)
  segments; there are no DPL-3 descriptors and no TSS.
- Paging (`kernel/paging.c:51`) maps every page `PAGE_PRESENT | PAGE_WRITE`
  (supervisor-only); the `PAGE_USER` bit is never set.
- `cc`-compiled programs run via `prog_exec` (`kernel/cc/runtime.c:171`), which
  casts the loaded binary to a function pointer and **calls it directly** in
  Ring 0. Kernel services are reached through a function-pointer table at
  `SYSCALL_TABLE_ADDR = 0x1F0000` (`call [table+i*4]`, emitted by
  `cg_call_syscall`, `kernel/cc/codegen.c:381`) — a direct call, not a privilege
  transition.

A buggy or malicious `run` program can therefore crash or take over the whole
machine. This milestone introduces a real userland/kernel boundary: run programs
in **Ring 3** and route their kernel calls through an `int $0x80` syscall gate.

`ideas.md` already lists this as a goal ("Usermode — Ring 3 Prozesse,
Syscall-Interface (INT 0x80 oder SYSENTER)").

## Scope decision
Do it **incrementally**: keep the single identity-mapped address space, but run
`run`-programs in Ring 3 with only their own region marked user-accessible, and
trap kernel calls through `int $0x80`. This delivers genuine privilege
separation (Ring 3 cannot execute privileged instructions or touch supervisor
pages) without per-process page directories. True per-process address-space
isolation is a follow-on milestone (M15).

## Existing scaffolding we build on
- `idt_set_gate(num, base, sel, flags)` (`kernel/idt.c:68`) already accepts an
  arbitrary DPL flag byte — a Ring 3 gate is just `0xEE` instead of `0x8E`.
- `struct isr_frame` (`kernel/include/idt.h`) already includes `useresp` and
  `ss`, the fields the CPU pushes on a privilege-change interrupt.
- `isr_common_stub` (`kernel/isr_stubs.asm`) already saves/reloads `ds` and
  `iret`s — the path a syscall stub reuses.
- Kernel stack `stack_top` (`kernel/entry.asm`), timer, and paging are in place.

## Design

### 1. GDT + TSS — new `kernel/gdt.c` / `kernel/gdt.h`
Build a fresh kernel GDT in `kernel_main` and `lgdt` it, keeping kernel
selectors identical so existing code and IDT entries (`0x08`) keep working.

| Selector | Descriptor   | Access byte | Ring |
|----------|--------------|-------------|------|
| 0x08     | kernel code  | 0x9A        | 0    |
| 0x10     | kernel data  | 0x92        | 0    |
| 0x1B     | user code    | 0xFA        | 3    |
| 0x23     | user data    | 0xF2        | 3    |
| 0x28     | TSS          | 0x89        | —    |

- Add `struct tss_entry` with `ss0 = 0x10`, `esp0 = stack_top`, rest zeroed;
  load with `ltr 0x28`. `esp0` is the kernel stack the CPU switches to on every
  Ring 3→0 entry — without it, any interrupt from Ring 3 faults.

### 2. Syscall gate — `kernel/idt.c` + `kernel/isr_stubs.asm`
- Add `idt_set_gate(0x80, (uint32_t)syscall_stub, 0x08, 0xEE)`. The `0xEE` flag
  (present, **DPL 3**, 32-bit gate) is what allows a Ring 3 `int $0x80`.
- Route `0x80` through the existing `isr_common_stub` machinery; in the C
  `isr_handler`, branch `if (frame->int_no == 0x80) syscall_dispatch(frame);`.
- **Verify/fix**: the common stub must reload kernel `DS/ES` on entry and restore
  user segments from the frame on `iret`. This is load-bearing once Ring 3 is
  real (it is benign today because everything is Ring 0).

### 3. Syscall ABI + dispatcher — rework `kernel/cc/runtime.c`
Replace the Ring-0-only pointer table with a register/stack convention:
- `eax` = syscall number; args via registers (`ebx/ecx/edx`) or read from the
  user stack via `frame->useresp`.
- `syscall_dispatch(frame)` switches on `frame->eax`, calls the existing
  `sys_putchar / sys_puts / sys_printf / sys_peek / sys_poke / sys_memset /
  sys_exit`, and writes the return value to `frame->eax`.
- **Validate user pointers** (string args to `sys_puts`/`sys_printf`) lie inside
  the user region before dereferencing — otherwise Ring 3 can use the kernel as
  a confused deputy to read supervisor memory.

### 4. Compiler change — `kernel/cc/codegen.c`
Change `cg_call_syscall(index)` (`codegen.c:381`) from emitting
`FF 15 <table+i*4>` to emitting `mov eax, index` + `int 0x80`. Keep cdecl
arg-passing on the stack and have the dispatcher read args from `frame->useresp`,
so the parser's arg handling is unchanged. This is the main compiler edit.

### 5. Paging — `kernel/paging.c`
Add `#define PAGE_USER 0x4`. Mark **only** the program's region user-accessible:
- Program load region at `CG_LOAD_ADDR = 0x200000` (code+data) →
  `PRESENT|WRITE|USER`.
- A dedicated **user stack** page (e.g. just below 2 MB) → `PRESENT|WRITE|USER`.
- Everything else (kernel, framebuffer `0xB8000`/`0xA0000`, page tables,
  IDT/GDT/TSS) stays supervisor — so Ring 3 physically cannot touch it. Flush the
  TLB after changing flags.

### 6. Enter/exit Ring 3 — `kernel/cc/runtime.c` + small asm helper
Replace the direct `entry()` call (`runtime.c:171`) with:
1. Save the kernel return context (kernel `esp` + a return label).
2. Build an `iret` frame: push `0x23` (user SS), user ESP, EFLAGS (IF=1), `0x1B`
   (user CS), `0x200000` (entry EIP); `iret` → CPU drops to Ring 3 at the program.
3. Program runs unprivileged, calls services via `int $0x80`.
4. On `SYS_EXIT` (or `main` returning), the syscall handler — running in Ring 0 —
   restores the saved kernel `esp` and returns into `prog_exec` instead of
   `iret`-ing back to the finished program.

Step 4 is the fiddliest part (a mini context-switch back to the kernel from
inside the trap handler); the rest is standard.

## Build order (each step independently testable)
1. **GDT + TSS** reload — boot still works, no behavior change yet.
2. **Syscall gate + dispatcher + codegen `int 0x80`**, but still call programs in
   Ring 0. Verify `cc hello.c -r` and `run printf.bin` still print — validates the
   gate/ABI without the ring-transition risk.
3. **Paging USER bits** for the program region + user stack.
4. **Ring 3 entry/exit** in `prog_exec`. Programs are now truly unprivileged.

## Verification
- **Regression:** `hello.c` / `printf.c` still run and print correctly (now via
  `int 0x80` from Ring 3). Test headless in QEMU with the screendump/serial
  workflow.
- **Isolation proof:** a deliberate "malicious" test program — e.g.
  `*(char*)0xB8000 = 'X';` (a supervisor page) or a privileged instruction such as
  `cli` — must trigger a page fault / #GP that the kernel catches
  ("program killed: protection fault") instead of corrupting the system. That
  fault is the proof the boundary works.
- Confirm `kernel.bin` stays within the 128 KB size check.

## Risks / notes
- The Ring 3→kernel **exit path** is the main hazard (careful asm to restore the
  kernel stack from the trap handler).
- Single address space → no inter-process isolation yet; fine since only one
  `run` program executes at a time. Separate page directories per process is the
  natural M15.
- `cc` programs use absolute addresses based at `0x200000`, which all land in the
  user region, so pointer fixups need no change.
- No FPU/libc involvement; unaffected.

## Files touched
- New: `kernel/gdt.c`, `kernel/gdt.h` (+ register in `CMakeLists.txt`
  `KERNEL_SOURCES`).
- `kernel/idt.c` (syscall gate), `kernel/isr_stubs.asm` (syscall stub / segment
  handling), `kernel/include/idt.h` if a TSS/selector constant is shared.
- `kernel/paging.c` (`PAGE_USER`, user region + user stack).
- `kernel/cc/runtime.c` (syscall dispatcher, Ring 3 enter/exit, `prog_exec`).
- `kernel/cc/codegen.c` (`cg_call_syscall` → `int 0x80`).
- `kernel/kernel.c` (call `gdt_init()` early; load TSS).
