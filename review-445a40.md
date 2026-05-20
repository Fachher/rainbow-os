# Rainbow-OS Code Review — `445a40`

**Date:** 2026-05-20
**Branch:** `master`
**Reviewer:** Claude Opus 4.6

---

## Summary

Clean, well-structured hobby OS with good separation of concerns. 9 milestones complete (bootloader, VGA, serial, keyboard, shell, PMM, paging, FAT12, SVGA, HDD boot). The uncommitted diff adds HDD boot support by passing the boot drive through DL from Stage 1 to Stage 2.

3 bugs, 3 robustness issues, 4 minor findings.

---

## Bugs

### B1: Keyboard has no input ring buffer
**File:** `kernel/drivers/keyboard.c`
**Severity:** Medium

The keyboard IRQ handler calls `shell_putchar()` directly from interrupt context. There is no intermediate ring buffer. Consequences:
- No type-ahead capability while the shell processes a command
- If `shell_putchar` or VGA scrolling takes time during an IRQ, subsequent keystrokes may be lost
- Mixing interrupt-context writes with main-loop reads is fragile

**Fix:** Add a small ring buffer (e.g. 64 bytes) written by the IRQ handler, consumed by the shell main loop.

---

### B2: FAT12 cluster chain has no cycle guard
**File:** `kernel/fs/fat12.c` — `fat12_read_file()`
**Severity:** Medium

The cluster-follow loop:
```c
while (read < to_read && cluster >= 2 && cluster < 0xFF8) {
    ...
    cluster = fat12_read_fat(cluster);
}
```
has no iteration limit. A corrupted FAT with a cycle (e.g. cluster 3 → 4 → 3) causes an infinite loop that hangs the kernel.

**Fix:** Add a counter bounded by total data clusters:
```c
uint32_t max_clusters = (total_sectors - data_start_sector) / sectors_per_cluster;
uint32_t iterations = 0;
while (read < to_read && cluster >= 2 && cluster < 0xFF8 && iterations++ < max_clusters) {
```

---

### B3: `pmm_alloc_frame` returns 0 on OOM — ambiguous sentinel
**File:** `kernel/pmm.c` — `pmm_alloc_frame()`
**Severity:** Low

Physical address 0x00000 is a valid address. Returning 0 for out-of-memory is indistinguishable from "allocated frame 0" to callers. Frame 0 is marked used during init so this won't cause a real double-map today, but it's a latent bug if the PMM is ever generalized.

**Fix:** Return `0xFFFFFFFF` as the OOM sentinel, or use a separate status flag.

---

## Robustness Issues

### R1: Kernel size hardcoded to 64 sectors (32 KB)
**File:** `boot/stage2.asm` — `KERNEL_SECTORS equ 64`
**Severity:** High

As features are added, the kernel binary will silently exceed 32 KB. The bootloader will load a truncated kernel, causing undefined behavior at boot. There is no build-time or runtime validation.

**Fix (build-time):** Add a CMake check after building `kernel.bin`:
```cmake
add_custom_command(TARGET kernel.elf POST_BUILD
    COMMAND test $(wc -c < kernel.bin) -le 32768
        || (echo "ERROR: kernel.bin exceeds 32KB (KERNEL_SECTORS=64)" && exit 1))
```
**Fix (long-term):** Write the kernel size into a header field that Stage 2 reads dynamically.

---

### R2: No multi-track disk reading in Stage 2
**File:** `boot/stage2.asm` — `load_kernel` routine
**Severity:** High (floppy), Low (HDD/QEMU)

INT 13h AH=02h cannot read across track boundaries. On a standard 1.44 MB floppy (18 sectors/track), reading 64 sectors starting at sector 6 (CHS 0/0/7) will cross the track boundary at sector 18. The BIOS will return an error or partial data.

This works on QEMU's HDD emulation (which uses LBA internally) but will fail on real floppy geometry or strict BIOS emulations.

**Fix:** Implement a read loop that calculates remaining sectors in the current track, reads up to the boundary, then advances head/cylinder.

---

### R3: `cat` command limited to 4 KB with no warning
**File:** `kernel/shell/shell.c` — `shell_execute()`, `cat` handler
**Severity:** Low

```c
static uint8_t file_buf[4096];
```
Files larger than 4096 bytes are silently truncated. The user sees partial content with no indication that data was cut off.

**Fix:** Compare `fat12_read_file` return value against file size from `fat12_list_root` and print a truncation warning.

---

## Minor Findings

### M1: `keyboard_wait_any` busy-waits without `hlt`
**File:** `kernel/drivers/keyboard.c` — `keyboard_wait_any()`
**Severity:** Low

The function polls the keyboard status port in a tight loop with no `hlt`. This pegs the (virtual) CPU at 100%.

**Fix:**
```c
while (!(inb(KBD_STATUS_PORT) & 1)) {
    __asm__ volatile("hlt");
}
```

---

### M2: VGA buffer pointer missing `volatile`
**File:** `kernel/drivers/vga.c`
**Severity:** Low

```c
static uint16_t *vga_buffer = (uint16_t *)VGA_BUFFER_ADDR;
```
This is a memory-mapped I/O region. Without `volatile`, the compiler may optimize away writes or reorder them. Same issue applies to framebuffer accesses in `kernel/drivers/svga.c`.

**Fix:** `static volatile uint16_t *vga_buffer = ...`

---

### M3: `saved_font` array is 2x larger than needed
**File:** `kernel/drivers/svga.c`
**Severity:** Trivial

```c
static uint8_t saved_font[8192];
```
The VGA font is 256 chars * 16 bytes/char = 4096 bytes. The array allocates 8192 bytes, wasting 4 KB of kernel BSS.

**Fix:** `static uint8_t saved_font[4096];` and update `sizeof(saved_font)` references.

---

### M4: Ramdisk `add_file` has no cluster overflow check
**File:** `kernel/fs/ramdisk.c` — `add_file()`
**Severity:** Low

`next_free_cluster++` increments without checking against total available clusters. Adding too many or too-large files writes past the ramdisk buffer, corrupting adjacent memory.

**Fix:** Check `next_free_cluster` against `TOTAL_SECTORS - DATA_SECTOR_START` before each allocation.

---

## What's Done Well

- **Boot drive passthrough:** DL correctly passed from Stage 1 → Stage 2 (new in this diff)
- **SVGA bounds checking:** `svga_putpixel` validates x/y before writing
- **Bank switch caching:** `current_bank` avoids redundant GR9 writes
- **FAT12 read capping:** `fat12_read_file` respects `buf_size` parameter
- **Shell buffer safety:** `cmd_buf` is length-limited to `CMD_MAX_LEN` with null termination
- **Reboot mechanism:** Triple-fault via null IDT is clean and reliable
- **Paging setup:** Identity map of 32 MB with proper page table alignment
- **Font save/restore:** VGA plane 2 font is preserved across SVGA mode switches
- **Clean init sequence:** `kernel_main` initializes subsystems in correct dependency order

---

## Recommended Priority

| # | Finding | Effort | Impact |
|---|---------|--------|--------|
| R1 | Kernel size validation | 10 min | Prevents silent boot corruption |
| R2 | Multi-track disk read | 1-2 hrs | Required for real hardware / larger kernels |
| B1 | Keyboard ring buffer | 30 min | Prevents lost keystrokes |
| B2 | FAT12 cycle guard | 5 min | Prevents kernel hang on corrupted FS |
| B3 | PMM OOM sentinel | 5 min | Cleaner error handling |
| M2 | `volatile` on MMIO | 5 min | Correctness under optimization |
| M1 | `hlt` in busy-wait | 2 min | Reduces CPU waste |
| R3 | `cat` truncation warning | 5 min | Better UX |
| M4 | Ramdisk overflow check | 5 min | Prevents memory corruption |
| M3 | `saved_font` size | 1 min | Saves 4 KB BSS |
