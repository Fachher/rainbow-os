#include "gdt.h"
#include "lib/string.h"
#include "drivers/serial.h"

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct tss_entry {
    uint32_t prev_tss;
    uint32_t esp0, ss0;
    uint32_t esp1, ss1;
    uint32_t esp2, ss2;
    uint32_t cr3, eip, eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap, iomap_base;
} __attribute__((packed));

static struct gdt_entry gdt[6];
static struct gdt_ptr   gdtp;
static struct tss_entry tss;

/* Dedicated ring-0 stack for entries from ring 3 (syscalls, IRQs, faults).
   Kept separate from the main kernel stack so a syscall handler never clobbers
   the frame of whatever kernel code launched the user program. */
static uint8_t kstack0[16384] __attribute__((aligned(16)));

static void gdt_set_gate(int n, uint32_t base, uint32_t limit,
                         uint8_t access, uint8_t gran) {
    gdt[n].base_low    = base & 0xFFFF;
    gdt[n].base_mid    = (base >> 16) & 0xFF;
    gdt[n].base_high   = (base >> 24) & 0xFF;
    gdt[n].limit_low   = limit & 0xFFFF;
    gdt[n].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[n].access      = access;
}

void tss_set_esp0(uint32_t esp0) {
    tss.esp0 = esp0;
}

void gdt_init(void) {
    gdtp.limit = sizeof(gdt) - 1;
    gdtp.base  = (uint32_t)&gdt;

    gdt_set_gate(0, 0, 0, 0, 0);                 /* null */
    gdt_set_gate(1, 0, 0xFFFFF, 0x9A, 0xCF);     /* 0x08 kernel code */
    gdt_set_gate(2, 0, 0xFFFFF, 0x92, 0xCF);     /* 0x10 kernel data */
    gdt_set_gate(3, 0, 0xFFFFF, 0xFA, 0xCF);     /* 0x18 user code (DPL3) */
    gdt_set_gate(4, 0, 0xFFFFF, 0xF2, 0xCF);     /* 0x20 user data (DPL3) */

    memset(&tss, 0, sizeof(tss));
    tss.ss0  = SEL_KDATA;
    tss.esp0 = (uint32_t)(kstack0 + sizeof(kstack0));
    tss.iomap_base = sizeof(tss);
    gdt_set_gate(5, (uint32_t)&tss, sizeof(tss) - 1, 0x89, 0x00); /* 0x28 TSS */

    /* Load GDT, reload segment registers, then load the task register. */
    __asm__ volatile(
        "lgdt (%0)\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        "ljmp $0x08, $1f\n"
        "1:\n"
        : : "r"(&gdtp) : "ax", "memory");

    __asm__ volatile("ltr %0" : : "r"((uint16_t)SEL_TSS));

    serial_log("GDT loaded (kernel+user segments, TSS)");
}
