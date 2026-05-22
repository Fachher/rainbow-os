#include "basic/exec.h"
#include "basic/token.h"
#include "basic/program.h"
#include "basic/tokenizer.h"
#include "basic/expr.h"
#include "basic/vars.h"
#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "lib/string.h"

/* Global executor state */
struct exec_state basic_state;

/* Error state */
static char error_msg[40];
static uint16_t error_line_num;
static bool has_error;

bool exec_has_error(void) { return has_error; }
const char *exec_error_msg(void) { return error_msg; }
uint16_t exec_error_line(void) { return error_line_num; }
void exec_clear_error(void) { has_error = false; error_msg[0] = '\0'; }

void exec_set_error(const char *msg) {
    has_error = true;
    int i = 0;
    while (msg[i] && i < 38) { error_msg[i] = msg[i]; i++; }
    error_msg[i] = '\0';
    if (basic_state.current_line)
        error_line_num = prog_line_number(basic_state.current_line);
    else
        error_line_num = 0;
    basic_state.running = false;
}

void exec_init(struct exec_state *state) {
    state->current_line = NULL;
    state->pos = 0;
    state->running = false;
    state->jumped = false;
    state->gosub_sp = 0;
    state->for_sp = 0;
}

/* Helper: peek token */
static uint8_t peek_tok(const uint8_t *tokens, uint8_t len, uint8_t pos) {
    if (pos >= len) return 0;
    return tokens[pos];
}

/* Helper: skip spaces */
static void skip_sp(const uint8_t *tokens, uint8_t len, uint8_t *pos) {
    while (*pos < len && tokens[*pos] == ' ') (*pos)++;
}

static bool is_letter(uint8_t c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}
static bool is_dig(uint8_t c) {
    return c >= '0' && c <= '9';
}
static uint8_t upcase(uint8_t c) {
    return (c >= 'a' && c <= 'z') ? c - 32 : c;
}

/* int-to-string for PRINT */
static void i32_to_str(int32_t val, char *buf) {
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    int i = 0;
    bool neg = false;
    if (val < 0) { neg = true; val = -val; }
    char tmp[12];
    while (val > 0) { tmp[i++] = '0' + (char)(val % 10); val /= 10; }
    int p = 0;
    if (neg) buf[p++] = '-';
    while (i > 0) buf[p++] = tmp[--i];
    buf[p] = '\0';
}

/* Read a line of input from keyboard into buf. Returns length. */
static uint8_t read_input_line(char *buf, uint8_t max) {
    uint8_t len = 0;
    while (1) {
        int c = keyboard_getchar();
        if (c == '\n') {
            vga_putchar('\n');
            buf[len] = '\0';
            return len;
        } else if (c == '\b') {
            if (len > 0) {
                len--;
                vga_putchar('\b');
            }
        } else if (c == KEY_CTRL('c')) {
            buf[0] = '\0';
            return 0;
        } else if (c >= ' ' && c < 127 && len < max - 1) {
            buf[len++] = (char)c;
            vga_putchar((char)c);
        }
    }
}

/* Parse variable target for LET/INPUT/FOR: returns name, index, is_string */
static bool parse_var_target(const uint8_t *tokens, uint8_t len, uint8_t *pos,
                             char *name, int *idx, bool *is_string) {
    skip_sp(tokens, len, pos);
    uint8_t tok = peek_tok(tokens, len, *pos);
    if (!is_letter(tok)) return false;

    *name = (char)upcase(tok);
    *idx = -1;
    *is_string = false;
    (*pos)++;

    if (peek_tok(tokens, len, *pos) == '$') {
        *is_string = true;
        (*pos)++;
    } else if (is_dig(peek_tok(tokens, len, *pos))) {
        *idx = tokens[*pos] - '0';
        (*pos)++;
    }
    return true;
}

/* Execute a single statement starting at pos in tokens */
static void exec_statement(struct exec_state *state, const uint8_t *tokens, uint8_t len) {
    skip_sp(tokens, len, &state->pos);
    if (state->pos >= len) return;

    uint8_t tok = tokens[state->pos];

    /* Check for Ctrl+C between statements */
    if (state->running && keyboard_has_key()) {
        int c = keyboard_getchar();
        if (c == KEY_CTRL('c')) {
            vga_write("\nBREAK");
            if (state->current_line) {
                vga_write(" IN ");
                char nbuf[8];
                i32_to_str(prog_line_number(state->current_line), nbuf);
                vga_write(nbuf);
            }
            vga_putchar('\n');
            state->running = false;
            return;
        }
    }

    switch (tok) {
    case TOK_PRINT: {
        state->pos++;
        bool newline = true;
        while (state->pos < len && tokens[state->pos] != ':' && tokens[state->pos] != TOK_ELSE) {
            skip_sp(tokens, len, &state->pos);
            if (state->pos >= len || tokens[state->pos] == ':' || tokens[state->pos] == TOK_ELSE) break;

            if (tokens[state->pos] == ';') {
                newline = false;
                state->pos++;
                continue;
            }
            if (tokens[state->pos] == ',') {
                /* Tab to next 14-column position */
                vga_putchar('\t');
                newline = false;
                state->pos++;
                continue;
            }

            expr_clear_error();
            struct bas_value v = expr_eval(tokens, len, &state->pos);
            if (expr_has_error()) {
                exec_set_error("SYNTAX ERROR");
                return;
            }
            if (v.type == VAL_NUM) {
                char nbuf[12];
                i32_to_str(v.num, nbuf);
                vga_write(nbuf);
            } else {
                for (uint8_t i = 0; i < v.str.len; i++)
                    vga_putchar(v.str.ptr[i]);
            }
            newline = true;
        }
        if (newline) vga_putchar('\n');
        break;
    }

    case TOK_INPUT: {
        state->pos++;
        skip_sp(tokens, len, &state->pos);

        /* Check for optional prompt string */
        if (peek_tok(tokens, len, state->pos) == TOK_STR_LIT) {
            state->pos++;
            uint8_t slen = tokens[state->pos++];
            for (uint8_t i = 0; i < slen; i++)
                vga_putchar((char)tokens[state->pos++]);
            skip_sp(tokens, len, &state->pos);
            if (peek_tok(tokens, len, state->pos) == ';') state->pos++;
        }

        vga_write("? ");

        char vname;
        int vidx;
        bool is_str;
        if (!parse_var_target(tokens, len, &state->pos, &vname, &vidx, &is_str)) {
            exec_set_error("SYNTAX ERROR");
            return;
        }

        char ibuf[80];
        read_input_line(ibuf, 80);

        if (is_str) {
            char *sv = var_str(vname);
            if (sv) strcpy(sv, ibuf);
        } else {
            int32_t *nv = var_num(vname, vidx);
            if (nv) {
                int32_t val = 0;
                int neg = 0;
                int i = 0;
                while (ibuf[i] == ' ') i++;
                if (ibuf[i] == '-') { neg = 1; i++; }
                while (ibuf[i] >= '0' && ibuf[i] <= '9') {
                    val = val * 10 + (ibuf[i] - '0');
                    i++;
                }
                *nv = neg ? -val : val;
            }
        }
        break;
    }

    case TOK_LET:
        state->pos++;
        /* fall through to variable assignment */
        goto do_assignment;

    default:
        /* Check for implicit LET: variable assignment */
        if (is_letter(tok)) {
do_assignment: ;
            char vname;
            int vidx;
            bool is_str;
            if (!parse_var_target(tokens, len, &state->pos, &vname, &vidx, &is_str)) {
                exec_set_error("SYNTAX ERROR");
                return;
            }
            skip_sp(tokens, len, &state->pos);
            if (peek_tok(tokens, len, state->pos) != '=') {
                exec_set_error("SYNTAX ERROR");
                return;
            }
            state->pos++;  /* skip '=' */

            expr_clear_error();
            struct bas_value v = expr_eval(tokens, len, &state->pos);
            if (expr_has_error()) { exec_set_error("SYNTAX ERROR"); return; }

            if (is_str) {
                if (v.type != VAL_STR) { exec_set_error("TYPE MISMATCH"); return; }
                char *sv = var_str(vname);
                if (sv) {
                    uint8_t copy_len = v.str.len;
                    if (copy_len > STR_MAX_LEN) copy_len = STR_MAX_LEN;
                    memcpy(sv, v.str.ptr, copy_len);
                    sv[copy_len] = '\0';
                }
            } else {
                if (v.type != VAL_NUM) { exec_set_error("TYPE MISMATCH"); return; }
                int32_t *nv = var_num(vname, vidx);
                if (nv) *nv = v.num;
            }
            break;
        }

        /* Other statements continue in this switch */
        switch (tok) {
        case TOK_IF: {
            state->pos++;
            expr_clear_error();
            struct bas_value cond = expr_eval(tokens, len, &state->pos);
            if (expr_has_error()) { exec_set_error("SYNTAX ERROR"); return; }

            skip_sp(tokens, len, &state->pos);
            if (peek_tok(tokens, len, state->pos) == TOK_THEN)
                state->pos++;

            bool is_true = (cond.type == VAL_NUM) ? (cond.num != 0) : (cond.str.len > 0);

            if (is_true) {
                skip_sp(tokens, len, &state->pos);
                /* Check if THEN is followed by a line number (implicit GOTO) */
                if (peek_tok(tokens, len, state->pos) == TOK_NUM) {
                    state->pos++;
                    int32_t target = (int32_t)tokens[state->pos]
                        | ((int32_t)tokens[state->pos + 1] << 8)
                        | ((int32_t)tokens[state->pos + 2] << 16)
                        | ((int32_t)tokens[state->pos + 3] << 24);
                    state->pos += 4;
                    uint8_t *line = prog_find_line((uint16_t)target);
                    if (!line) { exec_set_error("UNDEFINED LINE"); return; }
                    state->current_line = line;
                    state->jumped = true;
                } else {
                    exec_statement(state, tokens, len);
                }
            } else {
                /* Skip to ELSE or end of line */
                while (state->pos < len && tokens[state->pos] != TOK_ELSE)
                    state->pos++;
                if (state->pos < len && tokens[state->pos] == TOK_ELSE) {
                    state->pos++;
                    exec_statement(state, tokens, len);
                }
            }
            break;
        }

        case TOK_GOTO: {
            state->pos++;
            expr_clear_error();
            struct bas_value v = expr_eval(tokens, len, &state->pos);
            if (expr_has_error() || v.type != VAL_NUM) { exec_set_error("SYNTAX ERROR"); return; }
            uint8_t *line = prog_find_line((uint16_t)v.num);
            if (!line) { exec_set_error("UNDEFINED LINE"); return; }
            state->current_line = line;
            state->jumped = true;
            break;
        }

        case TOK_GOSUB: {
            state->pos++;
            expr_clear_error();
            struct bas_value v = expr_eval(tokens, len, &state->pos);
            if (expr_has_error() || v.type != VAL_NUM) { exec_set_error("SYNTAX ERROR"); return; }
            if (state->gosub_sp >= 32) { exec_set_error("OUT OF MEMORY"); return; }

            state->gosub_line[state->gosub_sp] = state->current_line;
            state->gosub_pos[state->gosub_sp] = state->pos;
            state->gosub_sp++;

            uint8_t *line = prog_find_line((uint16_t)v.num);
            if (!line) { exec_set_error("UNDEFINED LINE"); return; }
            state->current_line = line;
            state->jumped = true;
            break;
        }

        case TOK_RETURN: {
            state->pos++;
            if (state->gosub_sp == 0) { exec_set_error("RETURN WITHOUT GOSUB"); return; }
            state->gosub_sp--;
            state->current_line = state->gosub_line[state->gosub_sp];
            state->pos = state->gosub_pos[state->gosub_sp];
            /* Continue on the same line after the GOSUB */
            break;
        }

        case TOK_FOR: {
            state->pos++;
            char vname;
            int vidx;
            bool is_str;
            if (!parse_var_target(tokens, len, &state->pos, &vname, &vidx, &is_str) || is_str) {
                exec_set_error("SYNTAX ERROR"); return;
            }
            skip_sp(tokens, len, &state->pos);
            if (peek_tok(tokens, len, state->pos) != '=') { exec_set_error("SYNTAX ERROR"); return; }
            state->pos++;

            expr_clear_error();
            struct bas_value start_val = expr_eval(tokens, len, &state->pos);
            if (expr_has_error() || start_val.type != VAL_NUM) { exec_set_error("SYNTAX ERROR"); return; }

            skip_sp(tokens, len, &state->pos);
            if (peek_tok(tokens, len, state->pos) != TOK_TO) { exec_set_error("SYNTAX ERROR"); return; }
            state->pos++;

            expr_clear_error();
            struct bas_value limit_val = expr_eval(tokens, len, &state->pos);
            if (expr_has_error() || limit_val.type != VAL_NUM) { exec_set_error("SYNTAX ERROR"); return; }

            int32_t step = 1;
            skip_sp(tokens, len, &state->pos);
            if (peek_tok(tokens, len, state->pos) == TOK_STEP) {
                state->pos++;
                expr_clear_error();
                struct bas_value step_val = expr_eval(tokens, len, &state->pos);
                if (expr_has_error() || step_val.type != VAL_NUM) { exec_set_error("SYNTAX ERROR"); return; }
                step = step_val.num;
            }

            int32_t *vp = var_num(vname, vidx);
            if (!vp) { exec_set_error("SYNTAX ERROR"); return; }
            *vp = start_val.num;

            if (state->for_sp >= 8) { exec_set_error("OUT OF MEMORY"); return; }
            state->for_stack[state->for_sp].var = vname;
            state->for_stack[state->for_sp].var_idx = vidx;
            state->for_stack[state->for_sp].limit = limit_val.num;
            state->for_stack[state->for_sp].step = step;
            state->for_stack[state->for_sp].line = state->current_line;
            state->for_stack[state->for_sp].pos = state->pos;
            state->for_sp++;
            break;
        }

        case TOK_NEXT: {
            state->pos++;
            /* Optional variable name */
            char vname = 0;
            int vidx = -1;
            skip_sp(tokens, len, &state->pos);
            if (state->pos < len && is_letter(tokens[state->pos])) {
                vname = (char)upcase(tokens[state->pos]);
                state->pos++;
                if (state->pos < len && is_dig(tokens[state->pos])) {
                    vidx = tokens[state->pos] - '0';
                    state->pos++;
                }
            }

            if (state->for_sp == 0) { exec_set_error("NEXT WITHOUT FOR"); return; }

            /* Find matching FOR */
            int fi = state->for_sp - 1;
            if (vname) {
                while (fi >= 0 && (state->for_stack[fi].var != vname || state->for_stack[fi].var_idx != vidx))
                    fi--;
                if (fi < 0) { exec_set_error("NEXT WITHOUT FOR"); return; }
            }

            int32_t *vp = var_num(state->for_stack[fi].var, state->for_stack[fi].var_idx);
            if (!vp) { exec_set_error("SYNTAX ERROR"); return; }

            *vp += state->for_stack[fi].step;

            bool done;
            if (state->for_stack[fi].step > 0)
                done = (*vp > state->for_stack[fi].limit);
            else
                done = (*vp < state->for_stack[fi].limit);

            if (done) {
                /* Pop all FOR entries up to and including this one */
                state->for_sp = (uint8_t)fi;
            } else {
                /* Loop back */
                state->current_line = state->for_stack[fi].line;
                state->pos = state->for_stack[fi].pos;
                state->jumped = true;
            }
            break;
        }

        case TOK_REM:
            /* Skip rest of line */
            state->pos = len;
            break;

        case TOK_END:
            state->pos++;
            state->running = false;
            break;

        case TOK_POKE: {
            state->pos++;
            expr_clear_error();
            struct bas_value addr = expr_eval(tokens, len, &state->pos);
            if (expr_has_error() || addr.type != VAL_NUM) { exec_set_error("SYNTAX ERROR"); return; }
            skip_sp(tokens, len, &state->pos);
            if (peek_tok(tokens, len, state->pos) == ',') state->pos++;
            expr_clear_error();
            struct bas_value val = expr_eval(tokens, len, &state->pos);
            if (expr_has_error() || val.type != VAL_NUM) { exec_set_error("SYNTAX ERROR"); return; }
            *(volatile uint8_t *)(uint32_t)addr.num = (uint8_t)val.num;
            break;
        }

        case TOK_CLS:
            state->pos++;
            vga_clear();
            break;

        case TOK_CLR:
            state->pos++;
            vars_clear();
            break;

        default:
            exec_set_error("SYNTAX ERROR");
            break;
        }
        break;  /* break from outer default case */
    }
}

/* Execute one program line, starting at start_pos within the token data */
void exec_line_at(struct exec_state *state, const uint8_t *tokens, uint8_t len, uint8_t start_pos) {
    state->pos = start_pos;
    state->jumped = false;
    string_pool_reset();

    while (state->pos < len && state->running && !has_error) {
        skip_sp(tokens, len, &state->pos);
        if (state->pos >= len) break;

        /* Skip ':' statement separator */
        if (tokens[state->pos] == ':') {
            state->pos++;
            continue;
        }

        exec_statement(state, tokens, len);
        if (state->jumped) break;
    }
}

/* Execute one program line from the beginning */
void exec_line(struct exec_state *state, const uint8_t *tokens, uint8_t len) {
    exec_line_at(state, tokens, len, 0);
}

/* Execute in immediate mode */
void exec_immediate(const uint8_t *tokens, uint8_t len) {
    exec_init(&basic_state);
    basic_state.running = true;
    exec_clear_error();
    exec_line(&basic_state, tokens, len);
}
