#include "basic/tokenizer.h"
#include "basic/token.h"
#include "lib/string.h"

/* Keyword table: must check longer keywords first to avoid partial matches */
struct keyword {
    const char *name;
    uint8_t     token;
};

static const struct keyword keywords[] = {
    {"PRINT",   TOK_PRINT},
    {"INPUT",   TOK_INPUT},
    {"LET",     TOK_LET},
    {"IF",      TOK_IF},
    {"THEN",    TOK_THEN},
    {"GOTO",    TOK_GOTO},
    {"GOSUB",   TOK_GOSUB},
    {"RETURN",  TOK_RETURN},
    {"FOR",     TOK_FOR},
    {"TO",      TOK_TO},
    {"STEP",    TOK_STEP},
    {"NEXT",    TOK_NEXT},
    {"REM",     TOK_REM},
    {"END",     TOK_END},
    {"DIM",     TOK_DIM},
    {"ELSE",    TOK_ELSE},
    {"AND",     TOK_AND},
    {"OR",      TOK_OR},
    {"NOT",     TOK_NOT},
    {"MOD",     TOK_MOD},
    {"ABS",     TOK_ABS},
    {"RND",     TOK_RND},
    {"LEN",     TOK_LEN},
    {"VAL",     TOK_VAL},
    {"CHR$",    TOK_CHR},
    {"STR$",    TOK_STR},
    {"LEFT$",   TOK_LEFT},
    {"RIGHT$",  TOK_RIGHT},
    {"MID$",    TOK_MID},
    {"PEEK",    TOK_PEEK},
    {"POKE",    TOK_POKE},
    {"INT",     TOK_INT},
    {"LOAD",    TOK_LOAD},
    {"SAVE",    TOK_SAVE},
    {"LIST",    TOK_LIST},
    {"RUN",     TOK_RUN},
    {"NEW",     TOK_NEW},
    {"CLR",     TOK_CLR},
    {"CLS",     TOK_CLS},
    {NULL, 0}
};

static char to_upper(char c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    return c;
}

static bool is_alpha(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool is_alnum(char c) {
    return is_alpha(c) || is_digit(c);
}

/* Case-insensitive keyword match. Returns keyword length or 0. */
static int match_keyword(const char *src, const char *kw) {
    int i = 0;
    while (kw[i]) {
        if (to_upper(src[i]) != kw[i]) return 0;
        i++;
    }
    /* Keyword must not be followed by alphanumeric (except $ which is part of token) */
    if (kw[i - 1] != '$' && is_alnum(src[i])) return 0;
    return i;
}

static int32_t parse_number(const char *src, int *advance) {
    int32_t val = 0;
    int neg = 0;
    int i = 0;

    if (src[i] == '-') { neg = 1; i++; }

    while (is_digit(src[i])) {
        val = val * 10 + (src[i] - '0');
        i++;
    }
    *advance = i;
    return neg ? -val : val;
}

uint8_t tokenize(const char *source, uint8_t *out, uint8_t max_len) {
    uint8_t pos = 0;
    int si = 0;

    while (source[si] && pos < max_len - 5) {
        /* Skip spaces */
        if (source[si] == ' ') {
            out[pos++] = ' ';
            si++;
            continue;
        }

        /* Check for keywords */
        bool found = false;
        for (int k = 0; keywords[k].name; k++) {
            int klen = match_keyword(source + si, keywords[k].name);
            if (klen > 0) {
                out[pos++] = keywords[k].token;
                si += klen;
                found = true;

                /* After REM, store rest of line as raw ASCII */
                if (keywords[k].token == TOK_REM) {
                    while (source[si] && pos < max_len - 1) {
                        out[pos++] = (uint8_t)source[si++];
                    }
                }
                break;
            }
        }
        if (found) continue;

        /* Number literal */
        if (is_digit(source[si])) {
            int adv;
            int32_t val = parse_number(source + si, &adv);
            if (pos + 5 <= max_len) {
                out[pos++] = TOK_NUM;
                out[pos++] = (uint8_t)(val & 0xFF);
                out[pos++] = (uint8_t)((val >> 8) & 0xFF);
                out[pos++] = (uint8_t)((val >> 16) & 0xFF);
                out[pos++] = (uint8_t)((val >> 24) & 0xFF);
            }
            si += adv;
            continue;
        }

        /* String literal */
        if (source[si] == '"') {
            si++;  /* skip opening quote */
            uint8_t slen = 0;
            uint8_t start = pos;
            if (pos + 2 <= max_len) {
                out[pos++] = TOK_STR_LIT;
                pos++;  /* reserve space for length */
            }
            while (source[si] && source[si] != '"' && pos < max_len - 1) {
                out[pos++] = (uint8_t)source[si++];
                slen++;
            }
            if (source[si] == '"') si++;  /* skip closing quote */
            out[start + 1] = slen;
            continue;
        }

        /* Comparison operators: <=, >=, <> */
        if (source[si] == '<' && source[si + 1] == '=') {
            out[pos++] = TOK_LE; si += 2; continue;
        }
        if (source[si] == '>' && source[si + 1] == '=') {
            out[pos++] = TOK_GE; si += 2; continue;
        }
        if (source[si] == '<' && source[si + 1] == '>') {
            out[pos++] = TOK_NE; si += 2; continue;
        }

        /* Everything else: store as ASCII (variables, operators, punctuation) */
        out[pos++] = (uint8_t)source[si++];
    }

    return pos;
}

/* Reverse keyword table for detokenizer */
static const char *token_to_name(uint8_t tok) {
    for (int i = 0; keywords[i].name; i++) {
        if (keywords[i].token == tok) return keywords[i].name;
    }
    return NULL;
}

static void int_to_str(int32_t val, char *buf) {
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    int i = 0;
    bool neg = false;
    if (val < 0) { neg = true; val = -val; }

    char tmp[12];
    while (val > 0) {
        tmp[i++] = '0' + (char)(val % 10);
        val /= 10;
    }
    int pos = 0;
    if (neg) buf[pos++] = '-';
    while (i > 0) buf[pos++] = tmp[--i];
    buf[pos] = '\0';
}

void detokenize(const uint8_t *tokens, uint8_t len, char *out, uint8_t max_len) {
    uint8_t oi = 0;
    uint8_t ti = 0;

    while (ti < len && oi < max_len - 1) {
        uint8_t tok = tokens[ti];

        if (tok == TOK_NUM) {
            ti++;
            if (ti + 4 <= len) {
                int32_t val = (int32_t)tokens[ti]
                    | ((int32_t)tokens[ti + 1] << 8)
                    | ((int32_t)tokens[ti + 2] << 16)
                    | ((int32_t)tokens[ti + 3] << 24);
                ti += 4;
                char nbuf[12];
                int_to_str(val, nbuf);
                for (int i = 0; nbuf[i] && oi < max_len - 1; i++)
                    out[oi++] = nbuf[i];
            }
            continue;
        }

        if (tok == TOK_STR_LIT) {
            ti++;
            if (ti < len) {
                uint8_t slen = tokens[ti++];
                if (oi < max_len - 1) out[oi++] = '"';
                for (uint8_t i = 0; i < slen && ti < len && oi < max_len - 1; i++)
                    out[oi++] = (char)tokens[ti++];
                if (oi < max_len - 1) out[oi++] = '"';
            }
            continue;
        }

        if (tok == TOK_LE) { if (oi + 2 < max_len) { out[oi++] = '<'; out[oi++] = '='; } ti++; continue; }
        if (tok == TOK_GE) { if (oi + 2 < max_len) { out[oi++] = '>'; out[oi++] = '='; } ti++; continue; }
        if (tok == TOK_NE) { if (oi + 2 < max_len) { out[oi++] = '<'; out[oi++] = '>'; } ti++; continue; }

        if (tok >= 0x80) {
            const char *name = token_to_name(tok);
            if (name) {
                for (int i = 0; name[i] && oi < max_len - 1; i++)
                    out[oi++] = name[i];
                ti++;

                /* After REM, copy rest as raw text */
                if (tok == TOK_REM) {
                    while (ti < len && oi < max_len - 1)
                        out[oi++] = (char)tokens[ti++];
                }
                continue;
            }
        }

        /* ASCII character (variable, operator, punctuation) */
        out[oi++] = (char)tok;
        ti++;
    }

    out[oi] = '\0';
}
