/* Minimal x86 disassembler for the instruction subset emitted by the cc codegen
   (kernel/cc/codegen.c). Not a general decoder — unknown bytes become `db 0xNN`. */

#include "disasm.h"

/* --- tiny bounded string builder --------------------------------------- */
struct sb { char *p; char *end; };

static void s_ch(struct sb *b, char c) {
    if (b->p < b->end - 1) *b->p++ = c;
}
static void s_str(struct sb *b, const char *s) {
    while (*s) s_ch(b, *s++);
}
static void s_hex(struct sb *b, uint32_t v, int digits) {
    s_str(b, "0x");
    for (int i = digits - 1; i >= 0; i--) {
        int nib = (v >> (i * 4)) & 0xF;
        s_ch(b, nib < 10 ? '0' + nib : 'a' + nib - 10);
    }
}
static void s_dec(struct sb *b, int v) {
    if (v < 0) { s_ch(b, '-'); v = -v; }
    char t[12]; int n = 0;
    if (v == 0) t[n++] = '0';
    while (v > 0) { t[n++] = '0' + v % 10; v /= 10; }
    while (n > 0) s_ch(b, t[--n]);
}

/* [ebp + disp32] with signed display */
static void s_ebp_disp(struct sb *b, int32_t disp) {
    s_str(b, "[ebp");
    if (disp < 0) { s_ch(b, '-'); s_dec(b, -disp); }
    else          { s_ch(b, '+'); s_dec(b, disp); }
    s_ch(b, ']');
}

static int32_t rd32(const uint8_t *c) {
    return (int32_t)(c[0] | (c[1] << 8) | (c[2] << 16) | ((uint32_t)c[3] << 24));
}

int disasm_one(const uint8_t *c, uint32_t addr, char *out, int outsz) {
    struct sb b = { out, out + outsz };
    int len = 1;
    uint8_t op = c[0], m = c[1];

    switch (op) {
        case 0x50: s_str(&b, "push eax"); break;
        case 0x53: s_str(&b, "push ebx"); break;
        case 0x56: s_str(&b, "push esi"); break;
        case 0x57: s_str(&b, "push edi"); break;
        case 0x55: s_str(&b, "push ebp"); break;
        case 0x58: s_str(&b, "pop  eax"); break;
        case 0x59: s_str(&b, "pop  ecx"); break;
        case 0x5B: s_str(&b, "pop  ebx"); break;
        case 0x5D: s_str(&b, "pop  ebp"); break;
        case 0x5E: s_str(&b, "pop  esi"); break;
        case 0x5F: s_str(&b, "pop  edi"); break;
        case 0x91: s_str(&b, "xchg eax, ecx"); break;
        case 0x99: s_str(&b, "cdq"); break;
        case 0xC3: s_str(&b, "ret"); break;

        case 0x6A: s_str(&b, "push "); s_dec(&b, (int8_t)m); len = 2; break;
        case 0xCD: s_str(&b, "int  "); s_hex(&b, m, 2); len = 2; break;

        case 0xB8: s_str(&b, "mov  eax, "); s_hex(&b, (uint32_t)rd32(c+1), 8); len = 5; break;
        case 0xA1: s_str(&b, "mov  eax, ["); s_hex(&b, (uint32_t)rd32(c+1), 8); s_ch(&b, ']'); len = 5; break;
        case 0xA3: s_str(&b, "mov  ["); s_hex(&b, (uint32_t)rd32(c+1), 8); s_str(&b, "], eax"); len = 5; break;

        case 0xE9: s_str(&b, "jmp  "); s_hex(&b, addr + 5 + (uint32_t)rd32(c+1), 8); len = 5; break;
        case 0xE8: s_str(&b, "call "); s_hex(&b, addr + 5 + (uint32_t)rd32(c+1), 8); len = 5; break;

        case 0x01: if (m == 0xC8) { s_str(&b, "add  eax, ecx"); len = 2; } else goto unknown; break;
        case 0x29: if (m == 0xC1) { s_str(&b, "sub  ecx, eax"); len = 2; } else goto unknown; break;
        case 0x21: if (m == 0xC8) { s_str(&b, "and  eax, ecx"); len = 2; } else goto unknown; break;
        case 0x09: if (m == 0xC8) { s_str(&b, "or   eax, ecx"); len = 2; } else goto unknown; break;
        case 0x31: if (m == 0xC8) { s_str(&b, "xor  eax, ecx"); len = 2; } else goto unknown; break;
        case 0x39: if (m == 0xC1) { s_str(&b, "cmp  ecx, eax"); len = 2; } else goto unknown; break;
        case 0x88: if (m == 0x01) { s_str(&b, "mov  [ecx], al"); len = 2; } else goto unknown; break;
        case 0x85: if (m == 0xC0) { s_str(&b, "test eax, eax"); len = 2; } else goto unknown; break;

        case 0xD3:
            if (m == 0xE0) s_str(&b, "shl  eax, cl");
            else if (m == 0xE8) s_str(&b, "shr  eax, cl");
            else goto unknown;
            len = 2; break;

        case 0xF7:
            if (m == 0xF9) s_str(&b, "idiv ecx");
            else if (m == 0xD8) s_str(&b, "neg  eax");
            else if (m == 0xD0) s_str(&b, "not  eax");
            else goto unknown;
            len = 2; break;

        case 0x89:
            switch (m) {
                case 0xC8: s_str(&b, "mov  eax, ecx"); len = 2; break;
                case 0xD0: s_str(&b, "mov  eax, edx"); len = 2; break;
                case 0xE5: s_str(&b, "mov  ebp, esp"); len = 2; break;
                case 0xEC: s_str(&b, "mov  esp, ebp"); len = 2; break;
                case 0xE3: s_str(&b, "mov  ebx, esp"); len = 2; break;
                case 0x01: s_str(&b, "mov  [ecx], eax"); len = 2; break;
                case 0x85: s_str(&b, "mov  "); s_ebp_disp(&b, rd32(c+2)); s_str(&b, ", eax"); len = 6; break;
                case 0x44: if (c[2]==0x24){ s_str(&b,"mov  [esp+"); s_dec(&b,(int8_t)c[3]); s_str(&b,"], eax"); len=4; } else goto unknown; break;
                case 0x4C: if (c[2]==0x24){ s_str(&b,"mov  [esp+"); s_dec(&b,(int8_t)c[3]); s_str(&b,"], ecx"); len=4; } else goto unknown; break;
                default: goto unknown;
            }
            break;

        case 0x8B:
            switch (m) {
                case 0x85: s_str(&b, "mov  eax, "); s_ebp_disp(&b, rd32(c+2)); len = 6; break;
                case 0x00: s_str(&b, "mov  eax, [eax]"); len = 2; break;
                case 0x44: if (c[2]==0x24){ s_str(&b,"mov  eax, [esp+"); s_dec(&b,(int8_t)c[3]); s_ch(&b,']'); len=4; } else goto unknown; break;
                case 0x4C: if (c[2]==0x24){ s_str(&b,"mov  ecx, [esp+"); s_dec(&b,(int8_t)c[3]); s_ch(&b,']'); len=4; } else goto unknown; break;
                default: goto unknown;
            }
            break;

        case 0x8D:
            if (m == 0x85) { s_str(&b, "lea  eax, "); s_ebp_disp(&b, rd32(c+2)); len = 6; }
            else goto unknown;
            break;

        case 0x81:
            if (m == 0xC4) { s_str(&b, "add  esp, "); s_hex(&b, (uint32_t)rd32(c+2), 8); len = 6; }
            else if (m == 0xEC) { s_str(&b, "sub  esp, "); s_hex(&b, (uint32_t)rd32(c+2), 8); len = 6; }
            else goto unknown;
            break;

        case 0xC1:
            if (m == 0xE0) { s_str(&b, "shl  eax, "); s_dec(&b, c[2]); len = 3; }
            else goto unknown;
            break;

        case 0x0F:
            switch (m) {
                case 0xAF: if (c[2]==0xC1){ s_str(&b,"imul eax, ecx"); len=3; } else goto unknown; break;
                case 0xB6:
                    if (c[2]==0xC0) s_str(&b, "movzx eax, al");
                    else if (c[2]==0x00) s_str(&b, "movzx eax, byte [eax]");
                    else goto unknown;
                    len = 3; break;
                case 0x94: s_str(&b, "sete  al"); len = 3; break;
                case 0x95: s_str(&b, "setne al"); len = 3; break;
                case 0x9C: s_str(&b, "setl  al"); len = 3; break;
                case 0x9D: s_str(&b, "setge al"); len = 3; break;
                case 0x9E: s_str(&b, "setle al"); len = 3; break;
                case 0x9F: s_str(&b, "setg  al"); len = 3; break;
                case 0x84: s_str(&b, "jz   "); s_hex(&b, addr + 6 + (uint32_t)rd32(c+2), 8); len = 6; break;
                case 0x85: s_str(&b, "jnz  "); s_hex(&b, addr + 6 + (uint32_t)rd32(c+2), 8); len = 6; break;
                default: goto unknown;
            }
            break;

        default:
        unknown:
            s_str(&b, "db   "); s_hex(&b, op, 2); len = 1; break;
    }

    *b.p = '\0';
    return len;
}
