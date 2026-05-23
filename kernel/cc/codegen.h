#ifndef CC_CODEGEN_H
#define CC_CODEGEN_H

#include "include/types.h"

struct symbol;  /* forward decl */

#define CG_CODE_MAX     8192
#define CG_DATA_MAX     4096
#define CG_FIXUP_MAX    256
#define CG_LABEL_MAX    256
#define CG_STRING_MAX   2048

#define CG_LOAD_ADDR    0x200000   /* Programs loaded at 2 MB */

void cg_init(void);

/* Emit raw bytes */
void cg_emit_byte(uint8_t b);
void cg_emit_dword(uint32_t d);

/* Load values into EAX */
void cg_load_num(int32_t val);
void cg_load_global(int addr);
void cg_load_local(int offset);
void cg_lea_local(int offset);

/* Store EAX */
void cg_store_global(int addr);
void cg_store_local(int offset);

/* Stack ops */
void cg_push(void);
void cg_pop_into_ecx(void);
void cg_pop_to_eax(void);
void cg_add_esp(int bytes);

/* Arithmetic (ECX = left, EAX = right for sub/div) */
void cg_add(void);
void cg_sub(void);
void cg_mul(void);
void cg_div(void);
void cg_mod(void);

/* Bitwise */
void cg_bitand(void);
void cg_bitor(void);
void cg_bitxor(void);
void cg_shl(void);
void cg_shr(void);
void cg_shl_imm(int bits);

/* Unary */
void cg_negate(void);
void cg_logical_not(void);
void cg_bitwise_not(void);

/* Comparison (ECX = left, EAX = right) */
void cg_cmp(int op);

/* Pointer ops */
void cg_deref_dword(void);
void cg_deref_byte(void);
void cg_store_deref_dword(void);
void cg_store_deref_byte(void);

/* Labels and jumps */
int  cg_new_label(void);
void cg_label(int id);
void cg_jump(int label);
void cg_jump_zero(int label);
void cg_jump_nonzero(int label);

/* Functions */
void cg_func_begin(struct symbol *fn);
void cg_func_end(void);
void cg_call_symbol(struct symbol *fn);
void cg_return(void);

/* Strings */
int  cg_add_string(const char *s, int len);

/* Global data allocation */
int  cg_alloc_global(int bytes);

/* Output */
uint8_t *cg_output(int *out_size);

#endif
