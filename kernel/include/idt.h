#ifndef IDT_H
#define IDT_H

#include "types.h"

/* IDT entry (8 bytes each, 256 entries) */
struct idt_entry {
    uint16_t base_lo;       /* Lower 16 bits of handler address */
    uint16_t selector;      /* Kernel code segment selector */
    uint8_t  always0;
    uint8_t  flags;         /* Present, DPL, type */
    uint16_t base_hi;       /* Upper 16 bits of handler address */
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

/* Interrupt stack frame passed to C handlers */
struct isr_frame {
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;  /* pusha */
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;              /* pushed by CPU */
};

typedef void (*isr_handler_t)(struct isr_frame *frame);

void idt_init(void);
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);
void register_interrupt_handler(uint8_t n, isr_handler_t handler);

#endif
