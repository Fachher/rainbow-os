#include "cc/codegen.h"
#include "cc/sym.h"
#include "cc/token.h"
#include "cc/runtime.h"
#include "lib/string.h"

static uint8_t code[CG_CODE_MAX];
static int code_pos;

static int data_pos;

static uint8_t strings[CG_STRING_MAX];
static int string_pos;

/* Label positions: -1 = not yet defined */
static int label_pos[CG_LABEL_MAX];
static int num_labels;

/* Fixups: locations in code that need to be patched with label addresses */
struct fixup {
    int code_offset;   /* where the dword is in code[] */
    int label_id;      /* which label it refers to (-1 for symbol fixups) */
    struct symbol *sym; /* for function call fixups */
};
static struct fixup fixups[CG_FIXUP_MAX];
static int num_fixups;

/* String fixup: code positions where string offsets were emitted */
#define CG_STR_FIXUP_MAX 128
static int str_fixups[CG_STR_FIXUP_MAX];
static int num_str_fixups;

/* Global data allocation offset */
static int global_data_offset;

/* Function epilogue label for current function */
static int func_epilogue_label;

void cg_init(void) {
    code_pos = 0;
    data_pos = 0;
    string_pos = 0;
    num_labels = 0;
    num_fixups = 0;
    num_str_fixups = 0;
    global_data_offset = 0;
    memset(label_pos, 0xFF, sizeof(label_pos)); /* -1 */
}

void cg_emit_byte(uint8_t b) {
    if (code_pos < CG_CODE_MAX)
        code[code_pos++] = b;
}

void cg_emit_dword(uint32_t d) {
    cg_emit_byte(d & 0xFF);
    cg_emit_byte((d >> 8) & 0xFF);
    cg_emit_byte((d >> 16) & 0xFF);
    cg_emit_byte((d >> 24) & 0xFF);
}

/* Helper for relative jump fixups */
static void emit_rel_jump(uint8_t opcode, int label_id) {
    cg_emit_byte(opcode);
    /* Relative offset: target - (current_pos + 4) */
    if (num_fixups < CG_FIXUP_MAX) {
        fixups[num_fixups].code_offset = code_pos;
        fixups[num_fixups].label_id = label_id;
        fixups[num_fixups].sym = 0;
        num_fixups++;
    }
    cg_emit_dword(0);
}

static void emit_rel_jump2(uint8_t byte1, uint8_t byte2, int label_id) {
    cg_emit_byte(byte1);
    cg_emit_byte(byte2);
    if (num_fixups < CG_FIXUP_MAX) {
        fixups[num_fixups].code_offset = code_pos;
        fixups[num_fixups].label_id = label_id;
        fixups[num_fixups].sym = 0;
        num_fixups++;
    }
    cg_emit_dword(0);
}

/* --- Load/Store --- */

void cg_load_num(int32_t val) {
    /* mov eax, imm32 */
    cg_emit_byte(0xB8);
    cg_emit_dword((uint32_t)val);
}

void cg_load_global(int addr) {
    /* mov eax, [addr] — will be patched to absolute address in data section */
    cg_emit_byte(0xA1);
    /* Address = CG_LOAD_ADDR + code_size + data_offset; resolved at link time */
    if (num_fixups < CG_FIXUP_MAX) {
        fixups[num_fixups].code_offset = code_pos;
        fixups[num_fixups].label_id = -2; /* special: global data ref */
        fixups[num_fixups].sym = 0;
        /* Store the data offset in the placeholder */
        num_fixups++;
    }
    cg_emit_dword((uint32_t)addr);
}

void cg_load_local(int offset) {
    /* mov eax, [ebp + offset] */
    cg_emit_byte(0x8B);
    cg_emit_byte(0x85); /* ModRM: [ebp + disp32] */
    cg_emit_dword((uint32_t)offset);
}

void cg_lea_local(int offset) {
    /* lea eax, [ebp + offset] */
    cg_emit_byte(0x8D);
    cg_emit_byte(0x85);
    cg_emit_dword((uint32_t)offset);
}

void cg_store_global(int addr) {
    /* mov [addr], eax */
    cg_emit_byte(0xA3);
    if (num_fixups < CG_FIXUP_MAX) {
        fixups[num_fixups].code_offset = code_pos;
        fixups[num_fixups].label_id = -2;
        fixups[num_fixups].sym = 0;
        num_fixups++;
    }
    cg_emit_dword((uint32_t)addr);
}

void cg_store_local(int offset) {
    /* mov [ebp + offset], eax */
    cg_emit_byte(0x89);
    cg_emit_byte(0x85);
    cg_emit_dword((uint32_t)offset);
}

/* --- Stack ops --- */

void cg_push(void) {
    cg_emit_byte(0x50); /* push eax */
}

void cg_pop_into_ecx(void) {
    cg_emit_byte(0x59); /* pop ecx */
}

void cg_pop_to_eax(void) {
    cg_emit_byte(0x58); /* pop eax */
}

void cg_add_esp(int bytes) {
    /* add esp, imm32 */
    cg_emit_byte(0x81);
    cg_emit_byte(0xC4);
    cg_emit_dword((uint32_t)bytes);
}

/* --- Arithmetic --- */
/* Convention: ECX = left operand (popped), EAX = right operand */

void cg_add(void) {
    /* add eax, ecx  (result in eax) */
    cg_emit_byte(0x01);
    cg_emit_byte(0xC8); /* add eax, ecx */
}

void cg_sub(void) {
    /* ecx - eax -> eax:  sub ecx, eax; mov eax, ecx */
    cg_emit_byte(0x29); cg_emit_byte(0xC1); /* sub ecx, eax */
    cg_emit_byte(0x89); cg_emit_byte(0xC8); /* mov eax, ecx */
}

void cg_mul(void) {
    /* imul eax, ecx */
    cg_emit_byte(0x0F);
    cg_emit_byte(0xAF);
    cg_emit_byte(0xC1);
}

void cg_div(void) {
    /* ecx / eax:
       xchg eax, ecx; cdq; idiv ecx */
    cg_emit_byte(0x91);       /* xchg eax, ecx */
    cg_emit_byte(0x99);       /* cdq */
    cg_emit_byte(0xF7);
    cg_emit_byte(0xF9);       /* idiv ecx */
}

void cg_mod(void) {
    /* Same as div but result is in edx */
    cg_emit_byte(0x91);       /* xchg eax, ecx */
    cg_emit_byte(0x99);       /* cdq */
    cg_emit_byte(0xF7);
    cg_emit_byte(0xF9);       /* idiv ecx */
    cg_emit_byte(0x89); cg_emit_byte(0xD0); /* mov eax, edx */
}

/* --- Bitwise --- */

void cg_bitand(void) {
    cg_emit_byte(0x21); cg_emit_byte(0xC8); /* and eax, ecx */
}

void cg_bitor(void) {
    cg_emit_byte(0x09); cg_emit_byte(0xC8); /* or eax, ecx */
}

void cg_bitxor(void) {
    cg_emit_byte(0x31); cg_emit_byte(0xC8); /* xor eax, ecx */
}

void cg_shl(void) {
    /* ecx << eax: mov ecx_save to eax, shift. Actually:
       xchg eax, ecx; shl eax, cl */
    cg_emit_byte(0x91);       /* xchg eax, ecx */
    cg_emit_byte(0xD3);
    cg_emit_byte(0xE0);       /* shl eax, cl */
}

void cg_shr(void) {
    cg_emit_byte(0x91);       /* xchg eax, ecx */
    cg_emit_byte(0xD3);
    cg_emit_byte(0xE8);       /* shr eax, cl */
}

void cg_shl_imm(int bits) {
    /* shl eax, imm8 */
    cg_emit_byte(0xC1);
    cg_emit_byte(0xE0);
    cg_emit_byte((uint8_t)bits);
}

/* --- Unary --- */

void cg_negate(void) {
    /* neg eax */
    cg_emit_byte(0xF7);
    cg_emit_byte(0xD8);
}

void cg_logical_not(void) {
    /* test eax, eax; sete al; movzx eax, al */
    cg_emit_byte(0x85); cg_emit_byte(0xC0); /* test eax, eax */
    cg_emit_byte(0x0F); cg_emit_byte(0x94); cg_emit_byte(0xC0); /* sete al */
    cg_emit_byte(0x0F); cg_emit_byte(0xB6); cg_emit_byte(0xC0); /* movzx eax, al */
}

void cg_bitwise_not(void) {
    /* not eax */
    cg_emit_byte(0xF7);
    cg_emit_byte(0xD0);
}

/* --- Comparison --- */

void cg_cmp(int op) {
    /* cmp ecx, eax; setcc al; movzx eax, al */
    cg_emit_byte(0x39); cg_emit_byte(0xC1); /* cmp ecx, eax */
    cg_emit_byte(0x0F);
    switch (op) {
        case TOK_EQ: cg_emit_byte(0x94); break; /* sete */
        case TOK_NE: cg_emit_byte(0x95); break; /* setne */
        case TOK_LT: cg_emit_byte(0x9C); break; /* setl */
        case TOK_GT: cg_emit_byte(0x9F); break; /* setg */
        case TOK_LE: cg_emit_byte(0x9E); break; /* setle */
        case TOK_GE: cg_emit_byte(0x9D); break; /* setge */
        default: cg_emit_byte(0x94); break;
    }
    cg_emit_byte(0xC0); /* al */
    cg_emit_byte(0x0F); cg_emit_byte(0xB6); cg_emit_byte(0xC0); /* movzx eax, al */
}

/* --- Pointer ops --- */

void cg_deref_dword(void) {
    /* mov eax, [eax] */
    cg_emit_byte(0x8B);
    cg_emit_byte(0x00);
}

void cg_deref_byte(void) {
    /* movzx eax, byte [eax] */
    cg_emit_byte(0x0F);
    cg_emit_byte(0xB6);
    cg_emit_byte(0x00);
}

void cg_store_deref_dword(void) {
    /* mov [ecx], eax */
    cg_emit_byte(0x89);
    cg_emit_byte(0x01);
}

void cg_store_deref_byte(void) {
    /* mov [ecx], al */
    cg_emit_byte(0x88);
    cg_emit_byte(0x01);
}

/* --- Labels and jumps --- */

int cg_new_label(void) {
    if (num_labels >= CG_LABEL_MAX) return 0;
    int id = num_labels++;
    label_pos[id] = -1;
    return id;
}

void cg_label(int id) {
    if (id >= 0 && id < CG_LABEL_MAX)
        label_pos[id] = code_pos;
}

void cg_jump(int label) {
    emit_rel_jump(0xE9, label); /* jmp rel32 */
}

void cg_jump_zero(int label) {
    /* test eax, eax; jz rel32 */
    cg_emit_byte(0x85); cg_emit_byte(0xC0);
    emit_rel_jump2(0x0F, 0x84, label);
}

void cg_jump_nonzero(int label) {
    /* test eax, eax; jnz rel32 */
    cg_emit_byte(0x85); cg_emit_byte(0xC0);
    emit_rel_jump2(0x0F, 0x85, label);
}

/* --- Functions --- */

void cg_func_begin(struct symbol *fn) {
    fn->offset = code_pos; /* record function address */
    func_epilogue_label = cg_new_label();

    /* push ebp; mov ebp, esp */
    cg_emit_byte(0x55);
    cg_emit_byte(0x89); cg_emit_byte(0xE5);

    /* sub esp, N — placeholder, patched in cg_func_end */
    cg_emit_byte(0x81); cg_emit_byte(0xEC);
    cg_emit_dword(128); /* reserve 128 bytes for locals (enough for 32 vars) */

    /* Save callee-saved regs */
    cg_emit_byte(0x53); /* push ebx */
    cg_emit_byte(0x56); /* push esi */
    cg_emit_byte(0x57); /* push edi */
}

void cg_func_end(void) {
    cg_label(func_epilogue_label);

    /* Restore callee-saved regs */
    cg_emit_byte(0x5F); /* pop edi */
    cg_emit_byte(0x5E); /* pop esi */
    cg_emit_byte(0x5B); /* pop ebx */

    /* mov esp, ebp; pop ebp; ret */
    cg_emit_byte(0x89); cg_emit_byte(0xEC);
    cg_emit_byte(0x5D);
    cg_emit_byte(0xC3);
}

void cg_call_symbol(struct symbol *fn) {
    /* call rel32 */
    cg_emit_byte(0xE8);
    if (num_fixups < CG_FIXUP_MAX) {
        fixups[num_fixups].code_offset = code_pos;
        fixups[num_fixups].label_id = -3; /* symbol fixup */
        fixups[num_fixups].sym = fn;
        num_fixups++;
    }
    cg_emit_dword(0);
}

void cg_call_syscall(int index) {
    /* call [disp32]: FF 15 <addr32>
       Loads function pointer from syscall table and calls it */
    cg_emit_byte(0xFF);
    cg_emit_byte(0x15);
    cg_emit_dword(SYSCALL_TABLE_ADDR + index * 4);
}

void cg_reverse_stack(int n) {
    /* Reverse top n dwords on stack (converts L-to-R push to cdecl order).
       Swaps [esp+i*4] with [esp+(n-1-i)*4] using EAX/ECX as temps. */
    for (int i = 0; i < n / 2; i++) {
        int lo = i * 4;
        int hi = (n - 1 - i) * 4;
        /* mov eax, [esp + lo] */
        cg_emit_byte(0x8B); cg_emit_byte(0x44); cg_emit_byte(0x24);
        cg_emit_byte((uint8_t)lo);
        /* mov ecx, [esp + hi] */
        cg_emit_byte(0x8B); cg_emit_byte(0x4C); cg_emit_byte(0x24);
        cg_emit_byte((uint8_t)hi);
        /* mov [esp + lo], ecx */
        cg_emit_byte(0x89); cg_emit_byte(0x4C); cg_emit_byte(0x24);
        cg_emit_byte((uint8_t)lo);
        /* mov [esp + hi], eax */
        cg_emit_byte(0x89); cg_emit_byte(0x44); cg_emit_byte(0x24);
        cg_emit_byte((uint8_t)hi);
    }
}

void cg_return(void) {
    cg_jump(func_epilogue_label);
}

/* --- Strings --- */

int cg_add_string(const char *s, int len) {
    int offset = string_pos;
    for (int i = 0; i < len && string_pos < CG_STRING_MAX; i++) {
        strings[string_pos++] = (uint8_t)s[i];
    }
    if (string_pos < CG_STRING_MAX)
        strings[string_pos++] = 0; /* null terminator */
    /* Record the code position where the string offset will be emitted.
       The parser will call cg_load_num(offset) right after this, which emits
       B8 xx xx xx xx. The imm32 starts at code_pos+1 after B8. */
    if (num_str_fixups < CG_STR_FIXUP_MAX) {
        str_fixups[num_str_fixups++] = code_pos + 1; /* +1 to skip the B8 opcode */
    }
    return offset;
}

/* --- Global data --- */

int cg_alloc_global(int bytes) {
    int offset = global_data_offset;
    global_data_offset += bytes;
    /* Align to 4 bytes */
    global_data_offset = (global_data_offset + 3) & ~3;
    return offset;
}

/* --- Output --- */

/* Final binary layout:
   [code]
   [string data]
   [global data (BSS, zeroed)]

   Entry point: JMP to main
*/

static uint8_t output_buf[CG_CODE_MAX + CG_STRING_MAX + CG_DATA_MAX];

uint8_t *cg_output(int *out_size) {
    /* Find main */
    struct symbol *main_fn = sym_find_global("main");
    int main_offset = main_fn ? main_fn->offset : 0;

    /* Build output: first a JMP to main, then the rest of code */
    int out_pos = 0;

    /* Emit: jmp main (5 bytes) */
    output_buf[out_pos++] = 0xE9;
    /* E9 rel32: rel32 = target - (jmp_addr + 5) = (5 + main_offset) - 5 = main_offset */
    output_buf[out_pos++] = (uint8_t)(main_offset & 0xFF);
    output_buf[out_pos++] = (uint8_t)((main_offset >> 8) & 0xFF);
    output_buf[out_pos++] = (uint8_t)((main_offset >> 16) & 0xFF);
    output_buf[out_pos++] = (uint8_t)((main_offset >> 24) & 0xFF);

    int code_start = out_pos; /* = 5 */
    int string_start = code_start + code_pos;
    int data_start = string_start + string_pos;
    int total_size = data_start + global_data_offset;

    /* Copy code */
    memcpy(output_buf + code_start, code, code_pos);

    /* Resolve fixups */
    for (int i = 0; i < num_fixups; i++) {
        int co = fixups[i].code_offset + code_start; /* position in output_buf */
        if (fixups[i].label_id == -2) {
            /* Global data reference: stored offset is relative to data section */
            int data_off = (int)(output_buf[co] | (output_buf[co+1]<<8) |
                                 (output_buf[co+2]<<16) | (output_buf[co+3]<<24));
            uint32_t abs_addr = CG_LOAD_ADDR + data_start + data_off;
            output_buf[co]   = abs_addr & 0xFF;
            output_buf[co+1] = (abs_addr >> 8) & 0xFF;
            output_buf[co+2] = (abs_addr >> 16) & 0xFF;
            output_buf[co+3] = (abs_addr >> 24) & 0xFF;
        } else if (fixups[i].label_id == -3) {
            /* Symbol (function call) fixup: relative */
            struct symbol *fn = fixups[i].sym;
            int target = code_start + fn->offset;
            int32_t rel = target - (co + 4);
            output_buf[co]   = rel & 0xFF;
            output_buf[co+1] = (rel >> 8) & 0xFF;
            output_buf[co+2] = (rel >> 16) & 0xFF;
            output_buf[co+3] = (rel >> 24) & 0xFF;
        } else {
            /* Label fixup: relative jump */
            int lbl = fixups[i].label_id;
            int target = code_start + label_pos[lbl];
            int32_t rel = target - (co + 4);
            output_buf[co]   = rel & 0xFF;
            output_buf[co+1] = (rel >> 8) & 0xFF;
            output_buf[co+2] = (rel >> 16) & 0xFF;
            output_buf[co+3] = (rel >> 24) & 0xFF;
        }
    }

    /* Copy strings */
    memcpy(output_buf + string_start, strings, string_pos);

    /* Patch string references using the recorded fixup table */
    for (int i = 0; i < num_str_fixups; i++) {
        int co = str_fixups[i] + code_start;
        uint32_t str_off = output_buf[co] | (output_buf[co+1]<<8) |
                           (output_buf[co+2]<<16) | (output_buf[co+3]<<24);
        uint32_t abs_addr = CG_LOAD_ADDR + string_start + str_off;
        output_buf[co]   = abs_addr & 0xFF;
        output_buf[co+1] = (abs_addr >> 8) & 0xFF;
        output_buf[co+2] = (abs_addr >> 16) & 0xFF;
        output_buf[co+3] = (abs_addr >> 24) & 0xFF;
    }

    /* Zero-fill BSS (global data) */
    memset(output_buf + data_start, 0, global_data_offset);

    *out_size = total_size;
    return output_buf;
}
