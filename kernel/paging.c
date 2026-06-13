#include "include/paging.h"
#include "include/pmm.h"
#include "include/idt.h"
#include "lib/string.h"
#include "drivers/serial.h"
#include "drivers/vga.h"

/* Defined in cc/runtime.c: kill the current ring-3 program and return to the
   kernel. Only valid to call when a ring-3 program is running. */
extern void prog_fault(const char *reason);

/* Page directory: 1024 entries, each covering 4 MB */
static uint32_t page_directory[1024] __attribute__((aligned(PAGE_SIZE)));

/* Page tables for identity-mapping first 32 MB (8 tables x 1024 entries) */
#define IDENTITY_MAP_TABLES 8
static uint32_t page_tables[IDENTITY_MAP_TABLES][1024] __attribute__((aligned(PAGE_SIZE)));

static void page_fault_handler(struct isr_frame *frame) {
    uint32_t fault_addr;
    __asm__ volatile("mov %%cr2, %0" : "=r"(fault_addr));

    serial_write("PAGE FAULT at 0x");
    serial_write_hex(fault_addr);
    serial_write(" (err=0x");
    serial_write_hex(frame->err_code);
    serial_write(", eip=0x");
    serial_write_hex(frame->eip);
    serial_write(")\n");

    /* If the fault came from ring 3, the user program touched memory it isn't
       allowed to — kill it and return to the shell instead of halting. */
    if ((frame->cs & 3) == 3) {
        prog_fault("page fault");   /* does not return */
    }

    vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
    vga_write("\n*** PAGE FAULT at 0x");
    /* Inline hex print for VGA since we don't have vga_write_hex */
    char hex[9];
    for (int i = 7; i >= 0; i--) {
        uint8_t nibble = (fault_addr >> (i * 4)) & 0xF;
        hex[7 - i] = nibble < 10 ? '0' + nibble : 'A' + nibble - 10;
    }
    hex[8] = '\0';
    vga_write(hex);
    vga_write(" ***\n");

    /* Kernel fault — unrecoverable */
    __asm__ volatile("cli; hlt");
}

void paging_init(void) {
    /* Clear page directory */
    memset(page_directory, 0, sizeof(page_directory));

    /* Identity map first 32 MB: 8 page tables, each maps 4 MB */
    for (uint32_t t = 0; t < IDENTITY_MAP_TABLES; t++) {
        for (uint32_t i = 0; i < 1024; i++) {
            uint32_t phys = (t * 1024 + i) * PAGE_SIZE;
            page_tables[t][i] = phys | PAGE_PRESENT | PAGE_WRITE;
        }
        page_directory[t] = (uint32_t)&page_tables[t] | PAGE_PRESENT | PAGE_WRITE;
    }

    /* Mark the ring-3 program window [USER_REGION_BASE, USER_REGION_TOP) as
       user-accessible. The PDE and PTEs both need the USER bit (the CPU ANDs
       them), so only these pages become reachable from ring 3 — the kernel,
       framebuffer and page tables stay supervisor-only. */
    for (uint32_t addr = USER_REGION_BASE; addr < USER_REGION_TOP; addr += PAGE_SIZE) {
        uint32_t i = addr / PAGE_SIZE;
        page_tables[i / 1024][i % 1024] |= PAGE_USER;
        page_directory[i / 1024] |= PAGE_USER;
    }

    /* Register page fault handler (ISR 14) */
    register_interrupt_handler(14, page_fault_handler);

    /* Load page directory into CR3 and enable paging in CR0 */
    __asm__ volatile(
        "mov %0, %%cr3\n"
        "mov %%cr0, %%eax\n"
        "or $0x80000000, %%eax\n"
        "mov %%eax, %%cr0\n"
        :
        : "r"(page_directory)
        : "eax"
    );

    serial_log("Paging enabled (32 MB identity mapped)");
}
