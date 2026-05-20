#include "keyboard.h"
#include "serial.h"
#include "include/idt.h"
#include "include/pic.h"
#include "include/io.h"

#define KBD_DATA_PORT   0x60
#define KBD_STATUS_PORT 0x64

/* Ring buffer for decoupling IRQ handler from shell consumer */
#define KBD_BUF_SIZE 64
static char kbd_ring[KBD_BUF_SIZE];
static volatile uint8_t kbd_head;
static volatile uint8_t kbd_tail;

/* Scancode Set 1 -> ASCII (US layout, lowercase only for now) */
static const char scancode_ascii[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,  '\\','z','x','c','v','b','n','m',',','.','/', 0,
    '*', 0, ' ', 0,
    0,0,0,0,0,0,0,0,0,0,  /* F1-F10 */
    0, 0,                   /* Num Lock, Scroll Lock */
    0,0,0,'-',0,0,0,'+',0,0,0,0,0,  /* Numpad */
    0, 0,                   /* unused */
    0,0                     /* F11, F12 */
};

static const char scancode_ascii_shift[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,  'A','S','D','F','G','H','J','K','L',':','"','~',
    0,  '|','Z','X','C','V','B','N','M','<','>','?', 0,
    '*', 0, ' ', 0,
    0,0,0,0,0,0,0,0,0,0,
    0, 0,
    0,0,0,'-',0,0,0,'+',0,0,0,0,0,
    0, 0,
    0,0
};

static bool shift_held;
static volatile bool key_pressed;

static void keyboard_irq_handler(struct isr_frame *frame) {
    (void)frame;

    uint8_t scancode = inb(KBD_DATA_PORT);

    /* Key release (bit 7 set) */
    if (scancode & 0x80) {
        uint8_t released = scancode & 0x7F;
        /* Left Shift (0x2A) or Right Shift (0x36) released */
        if (released == 0x2A || released == 0x36) {
            shift_held = false;
        }
        return;
    }

    /* Shift pressed */
    if (scancode == 0x2A || scancode == 0x36) {
        shift_held = true;
        return;
    }

    char c;
    if (shift_held) {
        c = scancode_ascii_shift[scancode];
    } else {
        c = scancode_ascii[scancode];
    }

    key_pressed = true;
    if (c) {
        uint8_t next = (kbd_head + 1) % KBD_BUF_SIZE;
        if (next != kbd_tail) {
            kbd_ring[kbd_head] = c;
            kbd_head = next;
        }
    }
}

void keyboard_init(void) {
    shift_held = false;
    kbd_head = 0;
    kbd_tail = 0;

    /* Register IRQ1 handler (ISR 33) */
    register_interrupt_handler(33, keyboard_irq_handler);

    /* Unmask IRQ1 on PIC */
    pic_irq_unmask(1);

    /* Flush any pending data */
    while (inb(KBD_STATUS_PORT) & 0x01) {
        inb(KBD_DATA_PORT);
    }

    serial_log("PS/2 keyboard initialized (IRQ1)");
}

bool keyboard_has_key(void) {
    return kbd_head != kbd_tail;
}

char keyboard_getchar(void) {
    while (kbd_head == kbd_tail)
        __asm__ volatile("sti; hlt");
    char c = kbd_ring[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
    return c;
}

void keyboard_wait_any(void) {
    /* We may be called from inside the keyboard IRQ handler (e.g. gfx command).
     * The interrupt gate disables IF, and EOI hasn't been sent yet.
     * Fix both so the PIC can deliver the next keyboard IRQ. */
    pic_send_eoi(1);
    key_pressed = false;
    while (!key_pressed)
        __asm__ volatile("sti; hlt");
}
