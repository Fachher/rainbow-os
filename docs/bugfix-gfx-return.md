# Bugfix: GFX command could not return to shell

## Problem

Running the `gfx` command in the shell entered the SVGA graphics demo (640x480x8bpp) but could never return to text mode. Pressing a key had no effect. Additionally, even when the return was forced, the text mode displayed garbage blocks instead of readable characters.

## Root Causes

### 1. Keyboard IRQ blocked inside interrupt handler

The `gfx` command executes inside the keyboard IRQ handler's call stack:

```
IRQ1 (Enter key)
  -> keyboard_irq_handler()
    -> shell_putchar('\n')
      -> shell_execute("gfx")
        -> keyboard_wait_any()   <-- stuck here
```

Two issues prevented the wait from ever completing:

- **Interrupts disabled (IF=0):** The IDT uses interrupt gates (flags `0x8E`), which automatically clear the interrupt flag on entry. The `hlt` instruction with IF=0 halts the CPU permanently since no interrupt can wake it.
- **No EOI sent:** The PIC End-of-Interrupt is sent *after* the handler returns (in `isr_handler()`), but the handler never returns because it is stuck waiting. Without EOI, the PIC refuses to deliver further IRQ1 interrupts.

**Fix** (`kernel/drivers/keyboard.c`):
- Added `keyboard_wait_any()` which sends EOI for IRQ1 before waiting, and uses the atomic `sti; hlt` sequence to re-enable interrupts.
- Added a `volatile bool key_pressed` flag set by the IRQ handler on key-down events, replacing the broken direct port polling (`inb(0x64)`).

### 2. VGA font destroyed by graphics mode

After successfully returning to text mode, the screen showed blank/garbage blocks instead of characters. This happened because:

- In text mode, the character font is stored in VGA **plane 2** at address `0xA0000`.
- SVGA 256-color mode uses **chain-4** addressing, which maps all 4 VGA planes linearly. Framebuffer writes during the graphics demo overwrote the font data in plane 2.
- `vga_init()` only clears the text buffer at `0xB8000` -- it does not touch the font.

**Fix** (`kernel/drivers/svga.c`):
- Added `save_font()`: before entering graphics mode, temporarily remaps VGA to expose plane 2 at `0xA0000` and copies 8192 bytes (256 chars x 32 bytes) to a buffer.
- Added `restore_font()`: after switching back to text mode registers, writes the saved font back to plane 2.
- Key detail: GR6 must be set to `0x04` (A0000 mapping), not `0x0C` (B8000 mapping), to access the font plane.

## Files Changed

| File | Change |
|---|---|
| `kernel/drivers/keyboard.c` | Added `key_pressed` flag, `keyboard_wait_any()` with EOI + sti;hlt |
| `kernel/drivers/keyboard.h` | Exposed `keyboard_wait_any()` |
| `kernel/drivers/svga.c` | Added `save_font()` / `restore_font()` around mode switches |
| `kernel/shell/shell.c` | Replaced broken port-polling loop with `keyboard_wait_any()` |
