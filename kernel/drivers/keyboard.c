#include "keyboard.h"
#include "serial.h"
#include "include/idt.h"
#include "include/pic.h"
#include "include/io.h"

#define KBD_DATA_PORT   0x60
#define KBD_STATUS_PORT 0x64

/* Ring buffer stores int to support special keys > 127 */
#define KBD_BUF_SIZE 64
static int kbd_ring[KBD_BUF_SIZE];
static volatile uint8_t kbd_head;
static volatile uint8_t kbd_tail;

/* Scancode Set 1 -> ASCII (German QWERTZ layout) */
static const char scancode_ascii[128] = {
/*       Esc  1    2    3    4    5    6    7    8    9    0    ss   '    BS  */
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
/*  Tab  q    w    e    r    t    z    u    i    o    p    ue   +    Enter */
    '\t','q','w','e','r','t','z','u','i','o','p','[','+','\n',
/*  Ctrl a    s    d    f    g    h    j    k    l    oe   ae   ^    */
    0,  'a','s','d','f','g','h','j','k','l',';','\'','^',
/*  LSh  #    y    x    c    v    b    n    m    ,    .    -    RSh  */
    0,  '#','y','x','c','v','b','n','m',',','.','-', 0,
    '*', 0, ' ', 0,
    0,0,0,0,0,0,0,0,0,0,  /* F1-F10 */
    0, 0,                   /* Num Lock, Scroll Lock */
    0,0,0,'-',0,0,0,'+',0,0,0,0,0,  /* Numpad */
    0, 0,                   /* unused */
    '<',0                   /* < key, F12 */
};

static const char scancode_ascii_shift[128] = {
/*       Esc  !    "    §    $    %    &    /    (    )    =    ?    `    BS  */
    0,  27, '!','"','#','$','%','&','/','(',')','=','?','`', '\b',
/*  Tab  Q    W    E    R    T    Z    U    I    O    P    UE   *    Enter */
    '\t','Q','W','E','R','T','Z','U','I','O','P','{','*','\n',
/*  Ctrl A    S    D    F    G    H    J    K    L    OE   AE   °    */
    0,  'A','S','D','F','G','H','J','K','L',':','"','~',
/*  LSh  '    Y    X    C    V    B    N    M    ;    :    _    RSh  */
    0,  '\'','Y','X','C','V','B','N','M',';',':','_', 0,
    '*', 0, ' ', 0,
    0,0,0,0,0,0,0,0,0,0,
    0, 0,
    0,0,0,'-',0,0,0,'+',0,0,0,0,0,
    0, 0,
    '>',0
};

static bool shift_held;
static bool ctrl_held;
static bool extended;
static volatile bool key_pressed;

static void kbd_push(int key) {
    uint8_t next = (kbd_head + 1) % KBD_BUF_SIZE;
    if (next != kbd_tail) {
        kbd_ring[kbd_head] = key;
        kbd_head = next;
    }
}

static void keyboard_irq_handler(struct isr_frame *frame) {
    (void)frame;

    uint8_t scancode = inb(KBD_DATA_PORT);

    /* Extended scancode prefix */
    if (scancode == 0xE0) {
        extended = true;
        return;
    }

    if (extended) {
        extended = false;

        /* Extended key release — ignore */
        if (scancode & 0x80) return;

        int key = 0;
        switch (scancode) {
            case 0x48: key = KEY_UP;     break;
            case 0x50: key = KEY_DOWN;   break;
            case 0x4B: key = KEY_LEFT;   break;
            case 0x4D: key = KEY_RIGHT;  break;
            case 0x47: key = KEY_HOME;   break;
            case 0x4F: key = KEY_END;    break;
            case 0x49: key = KEY_PGUP;   break;
            case 0x51: key = KEY_PGDN;   break;
            case 0x53: key = KEY_DELETE; break;
        }
        if (key) {
            key_pressed = true;
            kbd_push(key);
        }
        return;
    }

    /* Key release (bit 7 set) */
    if (scancode & 0x80) {
        uint8_t released = scancode & 0x7F;
        if (released == 0x2A || released == 0x36) shift_held = false;
        if (released == 0x1D) ctrl_held = false;
        return;
    }

    /* Modifier press */
    if (scancode == 0x2A || scancode == 0x36) { shift_held = true; return; }
    if (scancode == 0x1D) { ctrl_held = true; return; }

    int c;
    if (shift_held) {
        c = scancode_ascii_shift[scancode];
    } else {
        c = scancode_ascii[scancode];
    }

    if (c) {
        if (ctrl_held && c >= 'a' && c <= 'z') {
            c = c & 0x1F;  /* Ctrl+letter */
        } else if (ctrl_held && c >= 'A' && c <= 'Z') {
            c = c & 0x1F;
        }
        key_pressed = true;
        kbd_push(c);
    }
}

void keyboard_init(void) {
    shift_held = false;
    ctrl_held = false;
    extended = false;
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

int keyboard_getchar(void) {
    while (kbd_head == kbd_tail)
        __asm__ volatile("sti; hlt");
    int c = kbd_ring[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
    return c;
}

void keyboard_wait_any(void) {
    pic_send_eoi(1);
    key_pressed = false;
    while (!key_pressed)
        __asm__ volatile("sti; hlt");
}
