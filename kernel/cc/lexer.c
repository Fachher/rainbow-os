#include "cc/lexer.h"
#include "lib/string.h"

static const char *src;
static int src_len;
static int pos;
static int line;

static int cur_tok;
static int cur_num;
static char cur_str[LEX_STR_MAX + 1];
static int cur_str_len;
static int has_peeked;

static int is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int is_hex(char c) {
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int is_alnum(char c) {
    return is_alpha(c) || is_digit(c);
}

static char next_char(void) {
    if (pos >= src_len) return '\0';
    char c = src[pos++];
    if (c == '\n') line++;
    return c;
}

static void skip_whitespace(void) {
    while (pos < src_len) {
        char c = src[pos];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            next_char();
        } else if (c == '/' && pos + 1 < src_len && src[pos + 1] == '/') {
            /* Line comment */
            while (pos < src_len && src[pos] != '\n') pos++;
        } else if (c == '/' && pos + 1 < src_len && src[pos + 1] == '*') {
            /* Block comment */
            pos += 2;
            while (pos + 1 < src_len) {
                if (src[pos] == '\n') line++;
                if (src[pos] == '*' && src[pos + 1] == '/') {
                    pos += 2;
                    break;
                }
                pos++;
            }
        } else {
            break;
        }
    }
}

static char parse_escape(void) {
    char c = next_char();
    switch (c) {
        case 'n': return '\n';
        case 't': return '\t';
        case 'r': return '\r';
        case '0': return '\0';
        case '\\': return '\\';
        case '\'': return '\'';
        case '"': return '"';
        default: return c;
    }
}

static int check_keyword(const char *s) {
    if (strcmp(s, "int") == 0) return TOK_INT;
    if (strcmp(s, "char") == 0) return TOK_CHAR;
    if (strcmp(s, "void") == 0) return TOK_VOID;
    if (strcmp(s, "if") == 0) return TOK_IF;
    if (strcmp(s, "else") == 0) return TOK_ELSE;
    if (strcmp(s, "while") == 0) return TOK_WHILE;
    if (strcmp(s, "for") == 0) return TOK_FOR;
    if (strcmp(s, "do") == 0) return TOK_DO;
    if (strcmp(s, "return") == 0) return TOK_RETURN;
    if (strcmp(s, "break") == 0) return TOK_BREAK;
    if (strcmp(s, "continue") == 0) return TOK_CONTINUE;
    return TOK_IDENT;
}

static int lex_scan(void) {
    skip_whitespace();
    if (pos >= src_len) return TOK_EOF;

    char c = next_char();

    /* Number literal */
    if (is_digit(c)) {
        cur_num = 0;
        if (c == '0' && pos < src_len && (src[pos] == 'x' || src[pos] == 'X')) {
            next_char(); /* skip 'x' */
            while (pos < src_len && is_hex(src[pos])) {
                char h = next_char();
                int v;
                if (h >= '0' && h <= '9') v = h - '0';
                else if (h >= 'a' && h <= 'f') v = h - 'a' + 10;
                else v = h - 'A' + 10;
                cur_num = cur_num * 16 + v;
            }
        } else {
            cur_num = c - '0';
            while (pos < src_len && is_digit(src[pos])) {
                cur_num = cur_num * 10 + (next_char() - '0');
            }
        }
        return TOK_NUM_LIT;
    }

    /* Identifier / keyword */
    if (is_alpha(c)) {
        cur_str_len = 0;
        cur_str[cur_str_len++] = c;
        while (pos < src_len && is_alnum(src[pos]) && cur_str_len < LEX_IDENT_MAX) {
            cur_str[cur_str_len++] = next_char();
        }
        cur_str[cur_str_len] = '\0';
        return check_keyword(cur_str);
    }

    /* String literal */
    if (c == '"') {
        cur_str_len = 0;
        while (pos < src_len && src[pos] != '"') {
            if (src[pos] == '\\') {
                next_char();
                cur_str[cur_str_len++] = parse_escape();
            } else {
                cur_str[cur_str_len++] = next_char();
            }
            if (cur_str_len >= LEX_STR_MAX) break;
        }
        if (pos < src_len) next_char(); /* skip closing " */
        cur_str[cur_str_len] = '\0';
        return TOK_STR_LIT;
    }

    /* Char literal */
    if (c == '\'') {
        if (pos < src_len && src[pos] == '\\') {
            next_char();
            cur_num = parse_escape();
        } else {
            cur_num = next_char();
        }
        if (pos < src_len && src[pos] == '\'') next_char();
        return TOK_CHAR_LIT;
    }

    /* Two-char operators */
    switch (c) {
        case '+':
            if (pos < src_len && src[pos] == '+') { next_char(); return TOK_INC; }
            if (pos < src_len && src[pos] == '=') { next_char(); return TOK_PLUSEQ; }
            return TOK_PLUS;
        case '-':
            if (pos < src_len && src[pos] == '-') { next_char(); return TOK_DEC; }
            if (pos < src_len && src[pos] == '=') { next_char(); return TOK_MINUSEQ; }
            return TOK_MINUS;
        case '*':
            if (pos < src_len && src[pos] == '=') { next_char(); return TOK_STAREQ; }
            return TOK_STAR;
        case '/':
            if (pos < src_len && src[pos] == '=') { next_char(); return TOK_SLASHEQ; }
            return TOK_SLASH;
        case '%': return TOK_PERCENT;
        case '&':
            if (pos < src_len && src[pos] == '&') { next_char(); return TOK_AND; }
            return TOK_AMP;
        case '|':
            if (pos < src_len && src[pos] == '|') { next_char(); return TOK_OR; }
            return TOK_PIPE;
        case '^': return TOK_CARET;
        case '~': return TOK_TILDE;
        case '!':
            if (pos < src_len && src[pos] == '=') { next_char(); return TOK_NE; }
            return TOK_BANG;
        case '=':
            if (pos < src_len && src[pos] == '=') { next_char(); return TOK_EQ; }
            return TOK_ASSIGN;
        case '<':
            if (pos < src_len && src[pos] == '=') { next_char(); return TOK_LE; }
            if (pos < src_len && src[pos] == '<') { next_char(); return TOK_SHL; }
            return TOK_LT;
        case '>':
            if (pos < src_len && src[pos] == '=') { next_char(); return TOK_GE; }
            if (pos < src_len && src[pos] == '>') { next_char(); return TOK_SHR; }
            return TOK_GT;
        case '(': return TOK_LPAREN;
        case ')': return TOK_RPAREN;
        case '{': return TOK_LBRACE;
        case '}': return TOK_RBRACE;
        case '[': return TOK_LBRACKET;
        case ']': return TOK_RBRACKET;
        case ';': return TOK_SEMI;
        case ',': return TOK_COMMA;
    }

    return TOK_EOF;
}

void lex_init(const char *source, int length) {
    src = source;
    src_len = length;
    pos = 0;
    line = 1;
    has_peeked = 0;
    cur_tok = TOK_EOF;
    cur_num = 0;
    cur_str[0] = '\0';
    cur_str_len = 0;
}

int lex_next(void) {
    if (has_peeked) {
        has_peeked = 0;
        return cur_tok;
    }
    cur_tok = lex_scan();
    return cur_tok;
}

int lex_peek(void) {
    if (!has_peeked) {
        /* Save state needed for peek */
        int save_num = cur_num;
        char save_str[LEX_STR_MAX + 1];
        int save_str_len = cur_str_len;
        memcpy(save_str, cur_str, cur_str_len + 1);

        cur_tok = lex_scan();
        has_peeked = 1;

        /* Peek must preserve str/num for current consumed token?
           No - peek returns the NEXT token, so cur_num/cur_str
           now belong to the peeked token. We need to let
           the caller access the peeked token's values. */
        (void)save_num;
        (void)save_str;
        (void)save_str_len;
    }
    return cur_tok;
}

int lex_num_val(void) {
    return cur_num;
}

char *lex_str_val(void) {
    return cur_str;
}

int lex_str_len(void) {
    return cur_str_len;
}

int lex_line(void) {
    return line;
}
