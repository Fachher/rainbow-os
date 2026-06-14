/* Interactive, machine-level debugger for ring-3 cc programs.
 *
 * The debugger lives in the #BP (int3) and #DB (single-step) handlers: when a
 * trap comes from the program (ring 3), it runs in ring 0 on the TSS esp0 stack
 * and opens a REPL. Modifying the trapped isr_frame (eip, eflags TF bit) and the
 * program's code bytes controls breakpoints, stepping and resume. */

#include "debugger.h"
#include "disasm.h"
#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "drivers/serial.h"
#include "include/idt.h"
#include "include/paging.h"
#include "cc/runtime.h"
#include "cc/codegen.h"

#define TF 0x100                     /* EFLAGS Trap Flag */
#define MAX_BP 16

extern void isr1(void);
extern void isr3(void);

struct bp { uint32_t addr; uint8_t orig; int active; };

static struct bp bps[MAX_BP];
static volatile int dbg_active;
static int pending_reinsert;         /* bp index to re-arm after a step, or -1 */
static int want_step;                /* resume should stop after one instruction */
static int at_bp;                    /* bp we are currently parked on, or -1 */

/* --- small output helpers ---------------------------------------------- */
static void puthex(uint32_t v, int digits) {
    for (int i = digits - 1; i >= 0; i--) {
        int n = (v >> (i * 4)) & 0xF;
        vga_putchar(n < 10 ? '0' + n : 'a' + n - 10);
    }
}
static void puts2(const char *s) { vga_write(s); }

/* --- breakpoint table --------------------------------------------------- */
static int in_user(uint32_t a) { return a >= USER_REGION_BASE && a < USER_REGION_TOP; }

static int find_bp(uint32_t addr) {
    for (int i = 0; i < MAX_BP; i++)
        if (bps[i].active && bps[i].addr == addr) return i;
    return -1;
}

static void arm_bp(int i)   { *(uint8_t *)bps[i].addr = 0xCC; }
static void disarm_bp(int i){ *(uint8_t *)bps[i].addr = bps[i].orig; }

static void set_bp(uint32_t addr) {
    if (!in_user(addr)) { puts2("  addr outside program\n"); return; }
    if (find_bp(addr) >= 0) { puts2("  bp exists\n"); return; }
    for (int i = 0; i < MAX_BP; i++) if (!bps[i].active) {
        bps[i].addr = addr;
        bps[i].orig = *(uint8_t *)addr;
        bps[i].active = 1;
        arm_bp(i);
        puts2("  bp set @ 0x"); puthex(addr, 8); vga_putchar('\n');
        return;
    }
    puts2("  bp table full\n");
}

static void del_bp(uint32_t addr) {
    int i = find_bp(addr);
    if (i < 0) { puts2("  no bp there\n"); return; }
    disarm_bp(i);
    bps[i].active = 0;
    puts2("  bp cleared\n");
}

static void clear_all_bps(void) {
    for (int i = 0; i < MAX_BP; i++)
        if (bps[i].active) { disarm_bp(i); bps[i].active = 0; }
}

/* read a code byte, showing the original (not 0xCC) where a bp is armed */
static uint8_t code_byte(uint32_t addr) {
    int i = find_bp(addr);
    return i >= 0 ? bps[i].orig : *(uint8_t *)addr;
}

/* --- views -------------------------------------------------------------- */
static void show_regs(struct isr_frame *f) {
    puts2("EAX="); puthex(f->eax, 8); puts2("  EBX="); puthex(f->ebx, 8);
    puts2("  ECX="); puthex(f->ecx, 8); puts2("  EDX="); puthex(f->edx, 8); vga_putchar('\n');
    puts2("ESI="); puthex(f->esi, 8); puts2("  EDI="); puthex(f->edi, 8);
    puts2("  EBP="); puthex(f->ebp, 8); puts2("  ESP="); puthex(f->useresp, 8); vga_putchar('\n');
    puts2("EIP="); puthex(f->eip, 8); puts2("  EFL="); puthex(f->eflags, 8);
    puts2("  ["); puts2(f->eflags & TF ? "T" : "-");
    puts2(f->eflags & 0x80 ? "S" : "-"); puts2(f->eflags & 0x40 ? "Z" : "-");
    puts2(f->eflags & 0x01 ? "C" : "-"); puts2("]\n");
}

static void show_mem(uint32_t addr, int n) {
    if (!in_user(addr)) { puts2("  addr outside program\n"); return; }
    for (int row = 0; row < n; row += 16) {
        puthex(addr + row, 8); puts2(": ");
        for (int i = 0; i < 16; i++) { puthex(*(uint8_t *)(addr + row + i), 2); vga_putchar(' '); }
        puts2(" ");
        for (int i = 0; i < 16; i++) {
            uint8_t c = *(uint8_t *)(addr + row + i);
            vga_putchar(c >= ' ' && c < 127 ? c : '.');
        }
        vga_putchar('\n');
    }
}

static void show_disasm(uint32_t addr) {
    for (int i = 0; i < 10; i++) {
        uint8_t buf[8];
        for (int j = 0; j < 8; j++) buf[j] = code_byte(addr + j);
        char m[64];
        int len = disasm_one(buf, addr, m, sizeof(m));
        puthex(addr, 8); puts2(":  "); puts2(m); vga_putchar('\n');
        addr += len;
    }
}

static void show_backtrace(struct isr_frame *f) {
    uint32_t eip = f->eip, ebp = f->ebp;
    puts2("#0  0x"); puthex(eip, 8); vga_putchar('\n');
    for (int n = 1; n < 16; n++) {
        if (!in_user(ebp) || ebp == 0) break;
        uint32_t ret = *(uint32_t *)(ebp + 4);
        uint32_t prev = *(uint32_t *)ebp;
        if (!in_user(ret)) break;
        puts2("#"); vga_write_dec(n); puts2("  0x"); puthex(ret, 8); vga_putchar('\n');
        ebp = prev;
    }
}

/* --- input -------------------------------------------------------------- */
static void read_line(char *buf, int max) {
    int n = 0;
    for (;;) {
        int ch = keyboard_getchar();
        if (ch == '\n') { vga_putchar('\n'); buf[n] = '\0'; return; }
        if (ch == '\b') { if (n > 0) { n--; vga_putchar('\b'); } continue; }
        if (ch >= ' ' && ch < 127 && n < max - 1) { buf[n++] = (char)ch; vga_putchar((char)ch); }
    }
}

static const char *skip_ws(const char *s) { while (*s == ' ') s++; return s; }

static uint32_t parse_hex(const char *s, int *ok) {
    s = skip_ws(s);
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    uint32_t v = 0; int any = 0;
    for (; *s; s++) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
        else break;
        v = v * 16 + d; any = 1;
    }
    if (ok) *ok = any;
    return v;
}

static void help(void) {
    puts2("commands:\n");
    puts2("  r            registers      x ADDR [N]  examine memory\n");
    puts2("  u [ADDR]     disassemble    bt          backtrace\n");
    puts2("  b ADDR       breakpoint     d ADDR      delete breakpoint\n");
    puts2("  s            step           c           continue\n");
    puts2("  q            quit\n");
}

/* --- the REPL (runs inside the trap handler) ---------------------------- */
static void dbg_repl(struct isr_frame *f) {
    char line[64];
    puts2("\n-- stopped @ 0x"); puthex(f->eip, 8); puts2(" --\n");
    show_disasm(f->eip);
    for (;;) {
        vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        puts2("(dbg) ");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        read_line(line, sizeof(line));
        const char *a = skip_ws(line);
        char cmd = a[0];
        const char *rest = a + 1;
        int ok;

        if (cmd == '\0') continue;
        if (a[0] == 'b' && a[1] == 't') { show_backtrace(f); continue; }  /* before 'b' */
        if (cmd == 'c') {                       /* continue */
            if (at_bp >= 0) { f->eflags |= TF; pending_reinsert = at_bp; want_step = 0; }
            else            { f->eflags &= ~TF; }
            return;
        }
        if (cmd == 's') {                       /* step */
            f->eflags |= TF; want_step = 1;
            if (at_bp >= 0) pending_reinsert = at_bp;
            return;
        }
        if (cmd == 'q') {                       /* quit: kill program */
            dbg_active = 0;
            clear_all_bps();
            prog_fault("debugger: quit");       /* does not return */
        }
        if (cmd == 'r') { show_regs(f); continue; }
        if (cmd == 'b') { uint32_t v = parse_hex(rest, &ok); if (ok) set_bp(v); else puts2("  usage: b ADDR\n"); continue; }
        if (cmd == 'd') { uint32_t v = parse_hex(rest, &ok); if (ok) del_bp(v); else puts2("  usage: d ADDR\n"); continue; }
        if (cmd == 'x') {
            uint32_t v = parse_hex(rest, &ok); if (!ok) { puts2("  usage: x ADDR [N]\n"); continue; }
            const char *r2 = skip_ws(rest); while (*r2 && *r2 != ' ') r2++;
            int ok2; uint32_t n = parse_hex(r2, &ok2);
            show_mem(v, ok2 ? (int)n : 64); continue;
        }
        if (cmd == 'u') { uint32_t v = parse_hex(rest, &ok); show_disasm(ok ? v : f->eip); continue; }
        if (a[0] == 'b' && a[1] == 't') { show_backtrace(f); continue; }
        if (cmd == 'h') { help(); continue; }
        puts2("  ? (h for help)\n");
    }
}

/* --- trap handlers ------------------------------------------------------ */
static void dbg_bp_handler(struct isr_frame *f) {
    if (!dbg_active || (f->cs & 3) != 3) { prog_fault("breakpoint"); return; }
    uint32_t bpaddr = f->eip - 1;            /* eip points just past the 0xCC */
    int idx = find_bp(bpaddr);
    if (idx >= 0) { f->eip = bpaddr; disarm_bp(idx); at_bp = idx; }
    else at_bp = -1;
    dbg_repl(f);
}

static void dbg_step_handler(struct isr_frame *f) {
    if (!dbg_active || (f->cs & 3) != 3) return;   /* ignore kernel-side #DB */
    if (pending_reinsert >= 0) { arm_bp(pending_reinsert); pending_reinsert = -1; }
    if (want_step) { at_bp = -1; dbg_repl(f); }
    else f->eflags &= ~TF;                          /* finished stepping off a bp */
}

/* --- entry -------------------------------------------------------------- */
void debugger_run(const char *filename) {
    uint32_t user_esp = prog_load(filename);
    if (!user_esp) return;

    for (int i = 0; i < MAX_BP; i++) bps[i].active = 0;
    pending_reinsert = -1; want_step = 0; at_bp = -1;

    register_interrupt_handler(1, dbg_step_handler);
    register_interrupt_handler(3, dbg_bp_handler);

    dbg_active = 1;
    set_bp(CG_LOAD_ADDR);                    /* stop at the first instruction */

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    puts2("[debugger] "); puts2(filename); puts2(" loaded. h=help, c=continue.\n");
    vga_set_color(VGA_WHITE, VGA_BLACK);

    enter_user(CG_LOAD_ADDR, user_esp);      /* returns on exit / quit / fault */

    dbg_active = 0;
    clear_all_bps();
    register_interrupt_handler(1, 0);
    register_interrupt_handler(3, 0);
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    puts2("[debugger] program finished\n");
    vga_set_color(VGA_WHITE, VGA_BLACK);
}
