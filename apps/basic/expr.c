#include "basic/expr.h"
#include "basic/token.h"
#include "basic/vars.h"
#include "lib/string.h"

/* String pool */
static char spool[STRING_POOL_SIZE];
static uint16_t spool_pos;

char *string_pool_alloc(uint8_t len) {
    if (spool_pos + len + 1 > STRING_POOL_SIZE) return NULL;
    char *p = &spool[spool_pos];
    spool_pos += len + 1;
    return p;
}

void string_pool_reset(void) {
    spool_pos = 0;
}

/* Error state */
static bool expr_error;

bool expr_has_error(void) { return expr_error; }
void expr_clear_error(void) { expr_error = false; }

/* LFSR pseudo-random number generator */
static uint32_t rnd_state = 12345;

static int32_t basic_rnd(int32_t max) {
    rnd_state ^= rnd_state << 13;
    rnd_state ^= rnd_state >> 17;
    rnd_state ^= rnd_state << 5;
    if (max <= 0) return 0;
    return (int32_t)(rnd_state % (uint32_t)max);
}

/* Forward declarations for recursive descent */
static struct bas_value parse_or(const uint8_t *tokens, uint8_t len, uint8_t *pos);

/* Helper: peek current token */
static uint8_t peek(const uint8_t *tokens, uint8_t len, uint8_t pos) {
    if (pos >= len) return 0;
    return tokens[pos];
}

/* Skip spaces in token stream */
static void skip_spaces(const uint8_t *tokens, uint8_t len, uint8_t *pos) {
    while (*pos < len && tokens[*pos] == ' ') (*pos)++;
}

/* Make a numeric value */
static struct bas_value make_num(int32_t n) {
    struct bas_value v;
    v.type = VAL_NUM;
    v.num = n;
    return v;
}

/* Make a string value */
static struct bas_value make_str(char *ptr, uint8_t len) {
    struct bas_value v;
    v.type = VAL_STR;
    v.str.ptr = ptr;
    v.str.len = len;
    return v;
}

/* Read a TOK_NUM from the stream */
static int32_t read_num_literal(const uint8_t *tokens, uint8_t *pos) {
    int32_t val = (int32_t)tokens[*pos]
        | ((int32_t)tokens[*pos + 1] << 8)
        | ((int32_t)tokens[*pos + 2] << 16)
        | ((int32_t)tokens[*pos + 3] << 24);
    *pos += 4;
    return val;
}

/* int-to-string helper */
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

/* String-to-int helper */
static int32_t str_to_i32(const char *s, uint8_t len) {
    int32_t val = 0;
    int neg = 0;
    uint8_t i = 0;
    while (i < len && s[i] == ' ') i++;
    if (i < len && s[i] == '-') { neg = 1; i++; }
    while (i < len && s[i] >= '0' && s[i] <= '9') {
        val = val * 10 + (s[i] - '0');
        i++;
    }
    return neg ? -val : val;
}

/* Is this token an upper-case letter? */
static bool is_upper(uint8_t c) {
    return c >= 'A' && c <= 'Z';
}
static bool is_lower(uint8_t c) {
    return c >= 'a' && c <= 'z';
}
static bool is_letter(uint8_t c) {
    return is_upper(c) || is_lower(c);
}
static bool is_dig(uint8_t c) {
    return c >= '0' && c <= '9';
}
static uint8_t upcase(uint8_t c) {
    return is_lower(c) ? c - 32 : c;
}

/* Parse a function call: expects '(' after function token */
static struct bas_value parse_function(uint8_t func_tok, const uint8_t *tokens, uint8_t len, uint8_t *pos) {
    skip_spaces(tokens, len, pos);
    if (peek(tokens, len, *pos) != '(') {
        expr_error = true;
        return make_num(0);
    }
    (*pos)++;  /* skip '(' */

    struct bas_value arg = parse_or(tokens, len, pos);
    if (expr_error) return make_num(0);

    /* Some functions take multiple args separated by ',' */
    struct bas_value arg2 = make_num(0);
    struct bas_value arg3 = make_num(0);

    switch (func_tok) {
    case TOK_ABS:
        skip_spaces(tokens, len, pos);
        if (peek(tokens, len, *pos) == ')') (*pos)++;
        if (arg.type != VAL_NUM) { expr_error = true; return make_num(0); }
        return make_num(arg.num < 0 ? -arg.num : arg.num);

    case TOK_RND:
        skip_spaces(tokens, len, pos);
        if (peek(tokens, len, *pos) == ')') (*pos)++;
        if (arg.type != VAL_NUM) { expr_error = true; return make_num(0); }
        return make_num(basic_rnd(arg.num));

    case TOK_INT:
        skip_spaces(tokens, len, pos);
        if (peek(tokens, len, *pos) == ')') (*pos)++;
        if (arg.type != VAL_NUM) { expr_error = true; return make_num(0); }
        return arg;

    case TOK_LEN:
        skip_spaces(tokens, len, pos);
        if (peek(tokens, len, *pos) == ')') (*pos)++;
        if (arg.type != VAL_STR) { expr_error = true; return make_num(0); }
        return make_num(arg.str.len);

    case TOK_VAL:
        skip_spaces(tokens, len, pos);
        if (peek(tokens, len, *pos) == ')') (*pos)++;
        if (arg.type != VAL_STR) { expr_error = true; return make_num(0); }
        return make_num(str_to_i32(arg.str.ptr, arg.str.len));

    case TOK_CHR: {
        skip_spaces(tokens, len, pos);
        if (peek(tokens, len, *pos) == ')') (*pos)++;
        if (arg.type != VAL_NUM) { expr_error = true; return make_num(0); }
        char *s = string_pool_alloc(1);
        if (!s) { expr_error = true; return make_num(0); }
        s[0] = (char)arg.num;
        s[1] = '\0';
        return make_str(s, 1);
    }

    case TOK_STR: {
        skip_spaces(tokens, len, pos);
        if (peek(tokens, len, *pos) == ')') (*pos)++;
        if (arg.type != VAL_NUM) { expr_error = true; return make_num(0); }
        char buf[12];
        i32_to_str(arg.num, buf);
        uint8_t l = (uint8_t)strlen(buf);
        char *s = string_pool_alloc(l);
        if (!s) { expr_error = true; return make_num(0); }
        memcpy(s, buf, l + 1);
        return make_str(s, l);
    }

    case TOK_PEEK:
        skip_spaces(tokens, len, pos);
        if (peek(tokens, len, *pos) == ')') (*pos)++;
        if (arg.type != VAL_NUM) { expr_error = true; return make_num(0); }
        return make_num(*(volatile uint8_t *)(uint32_t)arg.num);

    case TOK_LEFT: {
        /* LEFT$(s$, n) */
        skip_spaces(tokens, len, pos);
        if (peek(tokens, len, *pos) == ',') (*pos)++;
        arg2 = parse_or(tokens, len, pos);
        skip_spaces(tokens, len, pos);
        if (peek(tokens, len, *pos) == ')') (*pos)++;
        if (arg.type != VAL_STR || arg2.type != VAL_NUM) { expr_error = true; return make_num(0); }
        int32_t n = arg2.num;
        if (n < 0) n = 0;
        if (n > arg.str.len) n = arg.str.len;
        char *s = string_pool_alloc((uint8_t)n);
        if (!s) { expr_error = true; return make_num(0); }
        memcpy(s, arg.str.ptr, (size_t)n);
        s[n] = '\0';
        return make_str(s, (uint8_t)n);
    }

    case TOK_RIGHT: {
        /* RIGHT$(s$, n) */
        skip_spaces(tokens, len, pos);
        if (peek(tokens, len, *pos) == ',') (*pos)++;
        arg2 = parse_or(tokens, len, pos);
        skip_spaces(tokens, len, pos);
        if (peek(tokens, len, *pos) == ')') (*pos)++;
        if (arg.type != VAL_STR || arg2.type != VAL_NUM) { expr_error = true; return make_num(0); }
        int32_t n = arg2.num;
        if (n < 0) n = 0;
        if (n > arg.str.len) n = arg.str.len;
        int32_t start = arg.str.len - n;
        char *s = string_pool_alloc((uint8_t)n);
        if (!s) { expr_error = true; return make_num(0); }
        memcpy(s, arg.str.ptr + start, (size_t)n);
        s[n] = '\0';
        return make_str(s, (uint8_t)n);
    }

    case TOK_MID: {
        /* MID$(s$, pos, len) */
        skip_spaces(tokens, len, pos);
        if (peek(tokens, len, *pos) == ',') (*pos)++;
        arg2 = parse_or(tokens, len, pos);
        skip_spaces(tokens, len, pos);
        if (peek(tokens, len, *pos) == ',') (*pos)++;
        arg3 = parse_or(tokens, len, pos);
        skip_spaces(tokens, len, pos);
        if (peek(tokens, len, *pos) == ')') (*pos)++;
        if (arg.type != VAL_STR || arg2.type != VAL_NUM || arg3.type != VAL_NUM) {
            expr_error = true; return make_num(0);
        }
        int32_t p = arg2.num - 1;  /* BASIC MID$ is 1-based */
        int32_t n = arg3.num;
        if (p < 0) p = 0;
        if (p > arg.str.len) p = arg.str.len;
        if (n < 0) n = 0;
        if (p + n > arg.str.len) n = arg.str.len - p;
        char *s = string_pool_alloc((uint8_t)n);
        if (!s) { expr_error = true; return make_num(0); }
        memcpy(s, arg.str.ptr + p, (size_t)n);
        s[n] = '\0';
        return make_str(s, (uint8_t)n);
    }

    default:
        expr_error = true;
        return make_num(0);
    }
}

/* Primary: literals, variables, parenthesized expressions, functions, unary minus */
static struct bas_value parse_primary(const uint8_t *tokens, uint8_t len, uint8_t *pos) {
    skip_spaces(tokens, len, pos);

    uint8_t tok = peek(tokens, len, *pos);

    /* Unary minus */
    if (tok == '-') {
        (*pos)++;
        struct bas_value v = parse_primary(tokens, len, pos);
        if (v.type != VAL_NUM) { expr_error = true; return make_num(0); }
        return make_num(-v.num);
    }

    /* Unary NOT */
    if (tok == TOK_NOT) {
        (*pos)++;
        struct bas_value v = parse_primary(tokens, len, pos);
        if (v.type != VAL_NUM) { expr_error = true; return make_num(0); }
        return make_num(v.num ? 0 : 1);
    }

    /* Parenthesized expression */
    if (tok == '(') {
        (*pos)++;
        struct bas_value v = parse_or(tokens, len, pos);
        skip_spaces(tokens, len, pos);
        if (peek(tokens, len, *pos) == ')') (*pos)++;
        return v;
    }

    /* Number literal */
    if (tok == TOK_NUM) {
        (*pos)++;
        return make_num(read_num_literal(tokens, pos));
    }

    /* String literal */
    if (tok == TOK_STR_LIT) {
        (*pos)++;
        uint8_t slen = tokens[*pos]; (*pos)++;
        char *s = string_pool_alloc(slen);
        if (!s) { expr_error = true; return make_num(0); }
        memcpy(s, tokens + *pos, slen);
        s[slen] = '\0';
        *pos += slen;
        return make_str(s, slen);
    }

    /* Built-in functions */
    if (tok >= 0xA0 && tok <= 0xAB) {
        (*pos)++;
        return parse_function(tok, tokens, len, pos);
    }

    /* Variable: letter optionally followed by digit or $ */
    if (is_letter(tok)) {
        char name = (char)upcase(tok);
        (*pos)++;
        /* Check for string variable: A$ */
        if (peek(tokens, len, *pos) == '$') {
            (*pos)++;
            char *s = var_str(name);
            if (!s) { expr_error = true; return make_num(0); }
            uint8_t l = (uint8_t)strlen(s);
            char *pool = string_pool_alloc(l);
            if (!pool) { expr_error = true; return make_num(0); }
            memcpy(pool, s, l + 1);
            return make_str(pool, l);
        }
        /* Check for indexed numeric variable: A0-A9 */
        int idx = -1;
        if (is_dig(peek(tokens, len, *pos))) {
            idx = tokens[*pos] - '0';
            (*pos)++;
        }
        int32_t *vp = var_num(name, idx);
        if (!vp) { expr_error = true; return make_num(0); }
        return make_num(*vp);
    }

    /* Unexpected token */
    expr_error = true;
    return make_num(0);
}

/* Multiplication, division, MOD */
static struct bas_value parse_mul(const uint8_t *tokens, uint8_t len, uint8_t *pos) {
    struct bas_value left = parse_primary(tokens, len, pos);
    if (expr_error) return left;

    while (1) {
        skip_spaces(tokens, len, pos);
        uint8_t tok = peek(tokens, len, *pos);
        if (tok == '*' || tok == '/' || tok == TOK_MOD) {
            (*pos)++;
            struct bas_value right = parse_primary(tokens, len, pos);
            if (expr_error) return make_num(0);
            if (left.type != VAL_NUM || right.type != VAL_NUM) {
                expr_error = true; return make_num(0);
            }
            if (tok == '*') left.num *= right.num;
            else if (tok == '/') {
                if (right.num == 0) { expr_error = true; return make_num(0); }
                left.num /= right.num;
            } else {
                if (right.num == 0) { expr_error = true; return make_num(0); }
                left.num %= right.num;
            }
        } else break;
    }
    return left;
}

/* Addition, subtraction, string concatenation */
static struct bas_value parse_add(const uint8_t *tokens, uint8_t len, uint8_t *pos) {
    struct bas_value left = parse_mul(tokens, len, pos);
    if (expr_error) return left;

    while (1) {
        skip_spaces(tokens, len, pos);
        uint8_t tok = peek(tokens, len, *pos);
        if (tok == '+' || tok == '-') {
            (*pos)++;
            struct bas_value right = parse_mul(tokens, len, pos);
            if (expr_error) return make_num(0);

            /* String concatenation */
            if (tok == '+' && left.type == VAL_STR && right.type == VAL_STR) {
                uint8_t newlen = left.str.len + right.str.len;
                char *s = string_pool_alloc(newlen);
                if (!s) { expr_error = true; return make_num(0); }
                memcpy(s, left.str.ptr, left.str.len);
                memcpy(s + left.str.len, right.str.ptr, right.str.len);
                s[newlen] = '\0';
                left = make_str(s, newlen);
                continue;
            }
            if (left.type != VAL_NUM || right.type != VAL_NUM) {
                expr_error = true; return make_num(0);
            }
            if (tok == '+') left.num += right.num;
            else left.num -= right.num;
        } else break;
    }
    return left;
}

/* Comparison operators */
static struct bas_value parse_compare(const uint8_t *tokens, uint8_t len, uint8_t *pos) {
    struct bas_value left = parse_add(tokens, len, pos);
    if (expr_error) return left;

    skip_spaces(tokens, len, pos);
    uint8_t tok = peek(tokens, len, *pos);

    if (tok == '=' || tok == '<' || tok == '>' || tok == TOK_LE || tok == TOK_GE || tok == TOK_NE) {
        (*pos)++;
        struct bas_value right = parse_add(tokens, len, pos);
        if (expr_error) return make_num(0);

        /* String comparison */
        if (left.type == VAL_STR && right.type == VAL_STR) {
            int cmp = strcmp(left.str.ptr, right.str.ptr);
            int result = 0;
            if (tok == '=') result = (cmp == 0);
            else if (tok == TOK_NE) result = (cmp != 0);
            else if (tok == '<') result = (cmp < 0);
            else if (tok == '>') result = (cmp > 0);
            else if (tok == TOK_LE) result = (cmp <= 0);
            else if (tok == TOK_GE) result = (cmp >= 0);
            return make_num(result ? -1 : 0);  /* BASIC true = -1 */
        }

        if (left.type != VAL_NUM || right.type != VAL_NUM) {
            expr_error = true; return make_num(0);
        }
        int result = 0;
        if (tok == '=') result = (left.num == right.num);
        else if (tok == TOK_NE) result = (left.num != right.num);
        else if (tok == '<') result = (left.num < right.num);
        else if (tok == '>') result = (left.num > right.num);
        else if (tok == TOK_LE) result = (left.num <= right.num);
        else if (tok == TOK_GE) result = (left.num >= right.num);
        return make_num(result ? -1 : 0);
    }

    return left;
}

/* AND */
static struct bas_value parse_and(const uint8_t *tokens, uint8_t len, uint8_t *pos) {
    struct bas_value left = parse_compare(tokens, len, pos);
    if (expr_error) return left;

    while (1) {
        skip_spaces(tokens, len, pos);
        if (peek(tokens, len, *pos) == TOK_AND) {
            (*pos)++;
            struct bas_value right = parse_compare(tokens, len, pos);
            if (expr_error) return make_num(0);
            left = make_num((left.num && right.num) ? -1 : 0);
        } else break;
    }
    return left;
}

/* OR (top-level expression) */
static struct bas_value parse_or(const uint8_t *tokens, uint8_t len, uint8_t *pos) {
    struct bas_value left = parse_and(tokens, len, pos);
    if (expr_error) return left;

    while (1) {
        skip_spaces(tokens, len, pos);
        if (peek(tokens, len, *pos) == TOK_OR) {
            (*pos)++;
            struct bas_value right = parse_and(tokens, len, pos);
            if (expr_error) return make_num(0);
            left = make_num((left.num || right.num) ? -1 : 0);
        } else break;
    }
    return left;
}

/* Public entry point */
struct bas_value expr_eval(const uint8_t *tokens, uint8_t len, uint8_t *pos) {
    return parse_or(tokens, len, pos);
}
