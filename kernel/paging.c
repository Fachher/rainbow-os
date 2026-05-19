#include "include/paging.h"
#include "include/pmm.h"
#include "include/idt.h"
#include "lib/string.h"
#include "drivers/serial.h"
#include "drivers/vga.h"

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

    /* Halt — unrecoverable */
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
