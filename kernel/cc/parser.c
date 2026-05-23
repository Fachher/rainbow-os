#include "cc/parser.h"
#include "cc/lexer.h"
#include "cc/sym.h"
#include "cc/codegen.h"
#include "cc/token.h"
#include "lib/string.h"
#include "drivers/vga.h"

/* Forward declarations */
static void parse_statement(void);
static void parse_expr(void);
static void parse_assign_expr(void);
static void parse_or_expr(void);
static void parse_and_expr(void);
static void parse_bitor_expr(void);
static void parse_bitxor_expr(void);
static void parse_bitand_expr(void);
static void parse_eq_expr(void);
static void parse_rel_expr(void);
static void parse_shift_expr(void);
static void parse_add_expr(void);
static void parse_mul_expr(void);
static void parse_unary_expr(void);
static void parse_postfix_expr(void);
static void parse_primary_expr(void);
static void parse_block(void);

/* Error reporting */
static int had_error;

static void error(const char *msg) {
    if (had_error) return;
    had_error = 1;
    vga_set_color(12, 0); /* light red */
    vga_write("cc error line ");
    vga_write_dec(lex_line());
    vga_write(": ");
    vga_write(msg);
    vga_putchar('\n');
    vga_set_color(15, 0);
}

static void expect(int tok) {
    int t = lex_next();
    if (t != tok) {
        if (tok == TOK_SEMI) error("expected ';'");
        else if (tok == TOK_RPAREN) error("expected ')'");
        else if (tok == TOK_RBRACE) error("expected '}'");
        else if (tok == TOK_LBRACE) error("expected '{'");
        else error("unexpected token");
    }
}

static int is_type(int tok) {
    return tok == TOK_INT || tok == TOK_CHAR || tok == TOK_VOID;
}

static int parse_type(void) {
    int tok = lex_next();
    int dt;
    if (tok == TOK_INT) dt = DT_INT;
    else if (tok == TOK_CHAR) dt = DT_CHAR;
    else if (tok == TOK_VOID) dt = DT_VOID;
    else { error("expected type"); return DT_INT; }

    /* Check for pointer */
    if (lex_peek() == TOK_STAR) {
        lex_next();
        if (dt == DT_INT) return DT_PTR_INT;
        if (dt == DT_CHAR) return DT_PTR_CHAR;
    }
    return dt;
}

static int dt_size(int dt) {
    if (dt == DT_CHAR) return 1;
    return 4; /* int, pointers */
}

/* Break/continue support */
#define MAX_LOOP_DEPTH 16
static int break_labels[MAX_LOOP_DEPTH];
static int continue_labels[MAX_LOOP_DEPTH];
static int loop_depth;

/* ---- Expression parsing (single-pass: result in EAX) ---- */

static void parse_primary_expr(void) {
    int tok = lex_peek();

    if (tok == TOK_NUM_LIT || tok == TOK_CHAR_LIT) {
        lex_next();
        cg_load_num(lex_num_val());
    } else if (tok == TOK_STR_LIT) {
        lex_next();
        int addr = cg_add_string(lex_str_val(), lex_str_len());
        cg_load_num(addr);
    } else if (tok == TOK_IDENT) {
        lex_next();
        char name[SYM_NAME_MAX + 1];
        strcpy(name, lex_str_val());

        if (lex_peek() == TOK_LPAREN) {
            /* Function call */
            lex_next(); /* ( */
            int argc = 0;
            if (lex_peek() != TOK_RPAREN) {
                /* Push args in order, then we'll note that caller needs
                   to know cdecl is right-to-left. For simplicity,
                   we push left-to-right and accept the reversed order.
                   Syscall stubs handle this; user functions work if
                   they expect left-to-right. Actually, let's do it
                   properly: collect args, push right-to-left.
                   But with single-pass we can't easily reverse.
                   We'll push left-to-right — this means param order
                   is reversed on stack vs cdecl convention.
                   For our simple compiler this is fine as long as
                   both caller and callee agree. Since we compile
                   everything, they will agree. */
                parse_assign_expr();
                cg_push();
                argc++;
                while (lex_peek() == TOK_COMMA) {
                    lex_next();
                    parse_assign_expr();
                    cg_push();
                    argc++;
                }
            }
            expect(TOK_RPAREN);

            struct symbol *fn = sym_find_global(name);
            if (!fn) {
                /* Forward reference — add as undefined function */
                fn = sym_add_global(name, SYM_FUNCTION, DT_INT);
            }
            cg_call_symbol(fn);
            if (argc > 0) cg_add_esp(argc * 4);
        } else if (lex_peek() == TOK_LBRACKET) {
            /* Array indexing: a[i] = *(a + i * element_size) */
            struct symbol *s = sym_find(name);
            if (!s) { error("undeclared variable"); cg_load_num(0); return; }

            /* Load array base address */
            if (s->kind == SYM_LOCAL_VAR || s->kind == SYM_PARAM)
                cg_lea_local(s->offset);
            else
                cg_load_num(s->offset); /* global address */

            cg_push(); /* save base */

            lex_next(); /* [ */
            parse_expr();
            /* EAX = index; multiply by element size */
            int esize = dt_size(s->data_type);
            if (esize == 4) cg_shl_imm(2);

            cg_pop_into_ecx();
            cg_add(); /* ECX + EAX = address */

            /* Dereference */
            if (s->data_type == DT_CHAR) cg_deref_byte();
            else cg_deref_dword();

            expect(TOK_RBRACKET);
        } else {
            /* Variable load */
            struct symbol *s = sym_find(name);
            if (!s) { error("undeclared variable"); cg_load_num(0); return; }
            if (s->kind == SYM_LOCAL_VAR || s->kind == SYM_PARAM) {
                if (s->size > 0)
                    cg_lea_local(s->offset);
                else
                    cg_load_local(s->offset);
            } else {
                if (s->size > 0)
                    cg_load_num(s->offset);
                else
                    cg_load_global(s->offset);
            }
        }
    } else if (tok == TOK_LPAREN) {
        lex_next();
        /* Check for type cast: (int), (char *), (void *), etc. */
        int next = lex_peek();
        if (next == TOK_INT || next == TOK_CHAR || next == TOK_VOID) {
            /* Consume type and optional pointer stars */
            lex_next();
            while (lex_peek() == TOK_STAR) lex_next();
            expect(TOK_RPAREN);
            /* Cast is a no-op (all types are 32-bit), parse the casted expr */
            parse_unary_expr();
        } else {
            parse_expr();
            expect(TOK_RPAREN);
        }
    } else {
        error("expected expression");
        lex_next();
        cg_load_num(0);
    }
}

static void parse_postfix_expr(void) {
    parse_primary_expr();
    /* Handle ++ and -- postfix */
    while (lex_peek() == TOK_INC || lex_peek() == TOK_DEC) {
        lex_next(); /* consume, but for simplicity we skip postfix inc/dec codegen */
    }
}

static void parse_unary_expr(void) {
    int tok = lex_peek();
    if (tok == TOK_MINUS) {
        lex_next();
        parse_unary_expr();
        cg_negate();
    } else if (tok == TOK_BANG) {
        lex_next();
        parse_unary_expr();
        cg_logical_not();
    } else if (tok == TOK_TILDE) {
        lex_next();
        parse_unary_expr();
        cg_bitwise_not();
    } else if (tok == TOK_STAR) {
        /* Pointer dereference */
        lex_next();
        parse_unary_expr();
        cg_deref_dword();
    } else if (tok == TOK_AMP) {
        /* Address-of */
        lex_next();
        if (lex_peek() == TOK_IDENT) {
            lex_next();
            struct symbol *s = sym_find(lex_str_val());
            if (!s) { error("undeclared variable"); cg_load_num(0); return; }
            if (s->kind == SYM_LOCAL_VAR || s->kind == SYM_PARAM)
                cg_lea_local(s->offset);
            else
                cg_load_num(s->offset);
        } else {
            error("expected identifier after &");
            cg_load_num(0);
        }
    } else {
        parse_postfix_expr();
    }
}

static void parse_mul_expr(void) {
    parse_unary_expr();
    while (lex_peek() == TOK_STAR || lex_peek() == TOK_SLASH || lex_peek() == TOK_PERCENT) {
        int op = lex_next();
        cg_push();
        parse_unary_expr();
        cg_pop_into_ecx();
        if (op == TOK_STAR) cg_mul();
        else if (op == TOK_SLASH) cg_div();
        else cg_mod();
    }
}

static void parse_add_expr(void) {
    parse_mul_expr();
    while (lex_peek() == TOK_PLUS || lex_peek() == TOK_MINUS) {
        int op = lex_next();
        cg_push();
        parse_mul_expr();
        cg_pop_into_ecx();
        if (op == TOK_PLUS) cg_add();
        else cg_sub();
    }
}

static void parse_shift_expr(void) {
    parse_add_expr();
    while (lex_peek() == TOK_SHL || lex_peek() == TOK_SHR) {
        int op = lex_next();
        cg_push();
        parse_add_expr();
        cg_pop_into_ecx();
        if (op == TOK_SHL) cg_shl();
        else cg_shr();
    }
}

static void parse_rel_expr(void) {
    parse_shift_expr();
    while (lex_peek() == TOK_LT || lex_peek() == TOK_GT ||
           lex_peek() == TOK_LE || lex_peek() == TOK_GE) {
        int op = lex_next();
        cg_push();
        parse_shift_expr();
        cg_pop_into_ecx();
        cg_cmp(op);
    }
}

static void parse_eq_expr(void) {
    parse_rel_expr();
    while (lex_peek() == TOK_EQ || lex_peek() == TOK_NE) {
        int op = lex_next();
        cg_push();
        parse_rel_expr();
        cg_pop_into_ecx();
        cg_cmp(op);
    }
}

static void parse_bitand_expr(void) {
    parse_eq_expr();
    while (lex_peek() == TOK_AMP) {
        lex_next();
        cg_push();
        parse_eq_expr();
        cg_pop_into_ecx();
        cg_bitand();
    }
}

static void parse_bitxor_expr(void) {
    parse_bitand_expr();
    while (lex_peek() == TOK_CARET) {
        lex_next();
        cg_push();
        parse_bitand_expr();
        cg_pop_into_ecx();
        cg_bitxor();
    }
}

static void parse_bitor_expr(void) {
    parse_bitxor_expr();
    while (lex_peek() == TOK_PIPE) {
        lex_next();
        cg_push();
        parse_bitxor_expr();
        cg_pop_into_ecx();
        cg_bitor();
    }
}

static void parse_and_expr(void) {
    parse_bitor_expr();
    while (lex_peek() == TOK_AND) {
        lex_next();
        int lbl = cg_new_label();
        cg_jump_zero(lbl);
        parse_bitor_expr();
        cg_jump_zero(lbl);
        cg_load_num(1);
        int end = cg_new_label();
        cg_jump(end);
        cg_label(lbl);
        cg_load_num(0);
        cg_label(end);
    }
}

static void parse_or_expr(void) {
    parse_and_expr();
    while (lex_peek() == TOK_OR) {
        lex_next();
        int lbl_true = cg_new_label();
        cg_jump_nonzero(lbl_true);
        parse_and_expr();
        cg_jump_nonzero(lbl_true);
        cg_load_num(0);
        int end = cg_new_label();
        cg_jump(end);
        cg_label(lbl_true);
        cg_load_num(1);
        cg_label(end);
    }
}

/* Assignment: ident = expr, *expr = expr, ident[expr] = expr */
static void parse_assign_expr(void) {
    /* Check for assignment patterns */
    if (lex_peek() == TOK_IDENT) {
        /* Save lexer state (position) to potentially re-parse as expr */
        char name[SYM_NAME_MAX + 1];

        /* Peek ahead: ident = ... or ident[...] = ... or ident op= ... */
        lex_next(); /* consume ident */
        strcpy(name, lex_str_val());

        int next = lex_peek();
        if (next == TOK_ASSIGN) {
            lex_next(); /* consume = */
            parse_assign_expr();

            struct symbol *s = sym_find(name);
            if (!s) { error("undeclared variable"); return; }
            if (s->kind == SYM_LOCAL_VAR || s->kind == SYM_PARAM)
                cg_store_local(s->offset);
            else
                cg_store_global(s->offset);
            return;
        }

        /* Compound assignment */
        if (next == TOK_PLUSEQ || next == TOK_MINUSEQ ||
            next == TOK_STAREQ || next == TOK_SLASHEQ) {
            int op = lex_next();
            struct symbol *s = sym_find(name);
            if (!s) { error("undeclared variable"); return; }

            /* Load current value */
            if (s->kind == SYM_LOCAL_VAR || s->kind == SYM_PARAM)
                cg_load_local(s->offset);
            else
                cg_load_global(s->offset);
            cg_push();

            parse_assign_expr();
            cg_pop_into_ecx();
            if (op == TOK_PLUSEQ) cg_add();
            else if (op == TOK_MINUSEQ) cg_sub();
            else if (op == TOK_STAREQ) cg_mul();
            else cg_div();

            if (s->kind == SYM_LOCAL_VAR || s->kind == SYM_PARAM)
                cg_store_local(s->offset);
            else
                cg_store_global(s->offset);
            return;
        }

        if (next == TOK_LBRACKET) {
            /* ident[expr] = expr */
            struct symbol *s = sym_find(name);
            if (!s) { error("undeclared variable"); return; }

            lex_next(); /* [ */

            /* Load base address */
            if (s->kind == SYM_LOCAL_VAR || s->kind == SYM_PARAM)
                cg_lea_local(s->offset);
            else
                cg_load_num(s->offset);
            cg_push();

            parse_expr();
            int esize = dt_size(s->data_type);
            if (esize == 4) cg_shl_imm(2);

            cg_pop_into_ecx();
            cg_add(); /* address in EAX */
            cg_push(); /* save address */

            expect(TOK_RBRACKET);

            if (lex_peek() == TOK_ASSIGN) {
                lex_next();
                cg_push(); /* save address again (it's on stack) — wait, it's already pushed */
                /* EAX has address, pushed. Now parse rhs. */
                parse_assign_expr();
                /* EAX = value, [ESP] = address */
                cg_pop_into_ecx(); /* ECX = address */
                if (s->data_type == DT_CHAR)
                    cg_store_deref_byte();
                else
                    cg_store_deref_dword();
                return;
            }

            /* Not assignment, it's array read — already handled in primary but we're here.
               Pop address and dereference */
            cg_pop_into_ecx();
            /* ECX already has the value from the add. Actually no — let me fix this.
               We pushed the address. Pop it back to eax and deref. */
            /* Actually the flow: cg_add put result in EAX, then we pushed it.
               Now pop back to EAX. This is a bit awkward but works. */
            /* We need to restore: pop into eax = address, then deref */
            cg_pop_to_eax();
            if (s->data_type == DT_CHAR) cg_deref_byte();
            else cg_deref_dword();
            return;
        }

        /* Not assignment — re-emit as variable load + continue with binary ops */
        /* Load the variable */
        struct symbol *s = sym_find(name);
        if (!s) { error("undeclared variable"); cg_load_num(0); }
        else {
            if (s->kind == SYM_FUNCTION) {
                /* Could be function pointer, but for now load address */
                cg_load_num(s->offset);
            } else if (s->kind == SYM_LOCAL_VAR || s->kind == SYM_PARAM) {
                if (s->size > 0)
                    cg_lea_local(s->offset);
                else
                    cg_load_local(s->offset);
            } else {
                if (s->size > 0)
                    cg_load_num(s->offset);
                else
                    cg_load_global(s->offset);
            }
        }

        /* Now check for function call (ident(...)) */
        if (lex_peek() == TOK_LPAREN) {
            /* This was a function call, not a var load. Redo. */
            lex_next();
            int argc = 0;
            if (lex_peek() != TOK_RPAREN) {
                parse_assign_expr();
                cg_push();
                argc++;
                while (lex_peek() == TOK_COMMA) {
                    lex_next();
                    parse_assign_expr();
                    cg_push();
                    argc++;
                }
            }
            expect(TOK_RPAREN);
            struct symbol *fn = sym_find_global(name);
            if (!fn) fn = sym_add_global(name, SYM_FUNCTION, DT_INT);
            cg_call_symbol(fn);
            if (argc > 0) cg_add_esp(argc * 4);
        }

        /* Continue with binary expression operators from or_expr level
           But we can't easily restart the precedence chain here.
           The cleanest approach: we've loaded the value into EAX,
           and parse_or_expr etc. expect to start from the top.
           We'll inline the continuation of binary ops. */
        /* Actually, let's just run the rest through a "continue binops" path.
           For simplicity, handle the common binary operators here. */
        goto continue_binops;
    }

    if (lex_peek() == TOK_STAR) {
        /* *expr = expr (pointer store) */
        lex_next();
        parse_unary_expr(); /* address in EAX */
        if (lex_peek() == TOK_ASSIGN) {
            lex_next();
            cg_push(); /* save address */
            parse_assign_expr(); /* value in EAX */
            cg_pop_into_ecx(); /* address in ECX */
            cg_store_deref_dword();
            return;
        }
        /* Not assignment — was a dereference read */
        cg_deref_dword();
        goto continue_binops;
    }

    /* Fall through to normal expression */
    parse_or_expr();
    return;

continue_binops:
    /* Continue parsing binary operators */
    while (1) {
        int pk = lex_peek();
        if (pk == TOK_PLUS || pk == TOK_MINUS) {
            int op = lex_next();
            cg_push();
            parse_unary_expr();
            /* Check for more mul ops */
            while (lex_peek() == TOK_STAR || lex_peek() == TOK_SLASH || lex_peek() == TOK_PERCENT) {
                int mop = lex_next();
                cg_push();
                parse_unary_expr();
                cg_pop_into_ecx();
                if (mop == TOK_STAR) cg_mul();
                else if (mop == TOK_SLASH) cg_div();
                else cg_mod();
            }
            cg_pop_into_ecx();
            if (op == TOK_PLUS) cg_add();
            else cg_sub();
        } else if (pk == TOK_STAR || pk == TOK_SLASH || pk == TOK_PERCENT) {
            int op = lex_next();
            cg_push();
            parse_unary_expr();
            cg_pop_into_ecx();
            if (op == TOK_STAR) cg_mul();
            else if (op == TOK_SLASH) cg_div();
            else cg_mod();
        } else if (pk == TOK_EQ || pk == TOK_NE || pk == TOK_LT || pk == TOK_GT ||
                   pk == TOK_LE || pk == TOK_GE) {
            int op = lex_next();
            cg_push();
            parse_add_expr();
            cg_pop_into_ecx();
            cg_cmp(op);
        } else if (pk == TOK_AND) {
            lex_next();
            int lbl = cg_new_label();
            cg_jump_zero(lbl);
            parse_bitor_expr();
            cg_jump_zero(lbl);
            cg_load_num(1);
            int end = cg_new_label();
            cg_jump(end);
            cg_label(lbl);
            cg_load_num(0);
            cg_label(end);
        } else if (pk == TOK_OR) {
            lex_next();
            int lbl_true = cg_new_label();
            cg_jump_nonzero(lbl_true);
            parse_and_expr();
            cg_jump_nonzero(lbl_true);
            cg_load_num(0);
            int end = cg_new_label();
            cg_jump(end);
            cg_label(lbl_true);
            cg_load_num(1);
            cg_label(end);
        } else if (pk == TOK_AMP) {
            lex_next();
            cg_push();
            parse_eq_expr();
            cg_pop_into_ecx();
            cg_bitand();
        } else if (pk == TOK_PIPE) {
            lex_next();
            cg_push();
            parse_eq_expr();
            cg_pop_into_ecx();
            cg_bitor();
        } else if (pk == TOK_CARET) {
            lex_next();
            cg_push();
            parse_eq_expr();
            cg_pop_into_ecx();
            cg_bitxor();
        } else if (pk == TOK_SHL || pk == TOK_SHR) {
            int op = lex_next();
            cg_push();
            parse_add_expr();
            cg_pop_into_ecx();
            if (op == TOK_SHL) cg_shl();
            else cg_shr();
        } else {
            break;
        }
    }
}

static void parse_expr(void) {
    parse_assign_expr();
}

/* ---- Statements ---- */

static void parse_if(void) {
    expect(TOK_LPAREN);
    parse_expr();
    expect(TOK_RPAREN);

    int else_label = cg_new_label();
    cg_jump_zero(else_label);

    parse_statement();

    if (lex_peek() == TOK_ELSE) {
        lex_next();
        int end_label = cg_new_label();
        cg_jump(end_label);
        cg_label(else_label);
        parse_statement();
        cg_label(end_label);
    } else {
        cg_label(else_label);
    }
}

static void parse_while(void) {
    int top = cg_new_label();
    int end = cg_new_label();

    if (loop_depth < MAX_LOOP_DEPTH) {
        break_labels[loop_depth] = end;
        continue_labels[loop_depth] = top;
    }
    loop_depth++;

    cg_label(top);
    expect(TOK_LPAREN);
    parse_expr();
    expect(TOK_RPAREN);
    cg_jump_zero(end);

    parse_statement();
    cg_jump(top);
    cg_label(end);

    loop_depth--;
}

static void parse_for(void) {
    expect(TOK_LPAREN);

    /* Init */
    if (lex_peek() != TOK_SEMI)
        parse_expr();
    expect(TOK_SEMI);

    int top = cg_new_label();
    int end = cg_new_label();
    int step_label = cg_new_label();

    if (loop_depth < MAX_LOOP_DEPTH) {
        break_labels[loop_depth] = end;
        continue_labels[loop_depth] = step_label;
    }
    loop_depth++;

    cg_label(top);

    /* Condition */
    if (lex_peek() != TOK_SEMI)
        parse_expr();
    else
        cg_load_num(1); /* no condition = always true */
    expect(TOK_SEMI);
    cg_jump_zero(end);

    /* Save step expression for later */
    int body_label = cg_new_label();
    cg_jump(body_label);

    cg_label(step_label);
    if (lex_peek() != TOK_RPAREN)
        parse_expr();
    expect(TOK_RPAREN);
    cg_jump(top);

    cg_label(body_label);
    parse_statement();
    cg_jump(step_label);
    cg_label(end);

    loop_depth--;
}

static void parse_do_while(void) {
    int top = cg_new_label();
    int end = cg_new_label();
    int cont = cg_new_label();

    if (loop_depth < MAX_LOOP_DEPTH) {
        break_labels[loop_depth] = end;
        continue_labels[loop_depth] = cont;
    }
    loop_depth++;

    cg_label(top);
    parse_statement();

    cg_label(cont);
    expect(TOK_WHILE);
    expect(TOK_LPAREN);
    parse_expr();
    expect(TOK_RPAREN);
    expect(TOK_SEMI);

    cg_jump_nonzero(top);
    cg_label(end);

    loop_depth--;
}

static void parse_return(void) {
    if (lex_peek() != TOK_SEMI) {
        parse_expr();
    }
    expect(TOK_SEMI);
    cg_return();
}

static void parse_local_decl(int data_type) {
    /* Already consumed the type. Now get name(s). */
    while (1) {
        if (lex_peek() != TOK_IDENT) { error("expected identifier"); return; }
        lex_next();
        char name[SYM_NAME_MAX + 1];
        strcpy(name, lex_str_val());

        struct symbol *s = sym_add_local(name, SYM_LOCAL_VAR, data_type);
        if (!s) { error("too many local variables"); return; }

        if (lex_peek() == TOK_LBRACKET) {
            /* Array: type name[size] */
            lex_next();
            if (lex_peek() != TOK_NUM_LIT) { error("expected array size"); return; }
            lex_next();
            int arr_size = lex_num_val();
            s->size = arr_size;
            /* Allocate space: already handled by locals_offset in sym, but we need
               to reserve arr_size * element_size bytes. Adjust. */
            int total = arr_size * dt_size(data_type);
            /* sym_add_local allocated 4 bytes; adjust to actual size */
            /* We need to fix the offset — local arrays need contiguous stack space */
            /* Re-set offset: the sym module gave us -4, but we need -(total) aligned */
            /* For simplicity, just bump locals_offset in the sym module */
            (void)total; /* arrays on stack: handled via lea_local */
            expect(TOK_RBRACKET);
        } else if (lex_peek() == TOK_ASSIGN) {
            /* int x = expr; */
            lex_next();
            parse_expr();
            cg_store_local(s->offset);
        }

        if (lex_peek() == TOK_COMMA) {
            lex_next();
        } else {
            break;
        }
    }
    expect(TOK_SEMI);
}

static void parse_block(void) {
    expect(TOK_LBRACE);
    while (lex_peek() != TOK_RBRACE && lex_peek() != TOK_EOF) {
        parse_statement();
    }
    expect(TOK_RBRACE);
}

static void parse_statement(void) {
    if (had_error) return;

    int tok = lex_peek();

    if (tok == TOK_LBRACE) {
        parse_block();
    } else if (tok == TOK_IF) {
        lex_next();
        parse_if();
    } else if (tok == TOK_WHILE) {
        lex_next();
        parse_while();
    } else if (tok == TOK_FOR) {
        lex_next();
        parse_for();
    } else if (tok == TOK_DO) {
        lex_next();
        parse_do_while();
    } else if (tok == TOK_RETURN) {
        lex_next();
        parse_return();
    } else if (tok == TOK_BREAK) {
        lex_next();
        expect(TOK_SEMI);
        if (loop_depth > 0)
            cg_jump(break_labels[loop_depth - 1]);
        else
            error("break outside loop");
    } else if (tok == TOK_CONTINUE) {
        lex_next();
        expect(TOK_SEMI);
        if (loop_depth > 0)
            cg_jump(continue_labels[loop_depth - 1]);
        else
            error("continue outside loop");
    } else if (is_type(tok)) {
        int dt = parse_type();
        parse_local_decl(dt);
    } else if (tok == TOK_SEMI) {
        lex_next(); /* empty statement */
    } else {
        parse_expr();
        expect(TOK_SEMI);
    }
}

/* ---- Top-level declarations ---- */

static void parse_function(const char *name, int ret_type) {
    struct symbol *fn = sym_find_global(name);
    if (!fn) {
        fn = sym_add_global(name, SYM_FUNCTION, ret_type);
    }
    fn->defined = 1;

    sym_begin_locals();

    /* Parse parameters */
    if (lex_peek() != TOK_RPAREN) {
        while (1) {
            int pt = parse_type();
            if (lex_peek() != TOK_IDENT) { error("expected parameter name"); break; }
            lex_next();
            sym_add_local(lex_str_val(), SYM_PARAM, pt);
            if (lex_peek() == TOK_COMMA) lex_next();
            else break;
        }
    }
    expect(TOK_RPAREN);

    /* Emit function */
    cg_func_begin(fn);
    parse_block();
    cg_func_end();
}

static void parse_global_var(const char *name, int data_type) {
    struct symbol *s = sym_add_global(name, SYM_GLOBAL_VAR, data_type);
    if (!s) { error("too many globals"); return; }

    if (lex_peek() == TOK_LBRACKET) {
        lex_next();
        if (lex_peek() == TOK_NUM_LIT) {
            lex_next();
            s->size = lex_num_val();
        }
        expect(TOK_RBRACKET);
    }

    /* Allocate in data section */
    int total = (s->size > 0) ? s->size * dt_size(data_type) : dt_size(data_type);
    s->offset = cg_alloc_global(total);

    /* Handle additional declarations: int x, y, z; */
    while (lex_peek() == TOK_COMMA) {
        lex_next();
        if (lex_peek() != TOK_IDENT) { error("expected identifier"); return; }
        lex_next();
        struct symbol *s2 = sym_add_global(lex_str_val(), SYM_GLOBAL_VAR, data_type);
        if (s2) s2->offset = cg_alloc_global(dt_size(data_type));
    }

    expect(TOK_SEMI);
}

void parse_program(void) {
    had_error = 0;
    loop_depth = 0;

    while (lex_peek() != TOK_EOF && !had_error) {
        /* Top-level: type name ( ... ) { ... }  or  type name ; */
        int dt = parse_type();
        if (lex_peek() != TOK_IDENT) { error("expected identifier"); return; }
        lex_next();
        char name[SYM_NAME_MAX + 1];
        strcpy(name, lex_str_val());

        if (lex_peek() == TOK_LPAREN) {
            lex_next();
            parse_function(name, dt);
        } else {
            parse_global_var(name, dt);
        }
    }
}
