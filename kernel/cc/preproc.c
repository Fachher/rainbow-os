#include "cc/preproc.h"
#include "lib/string.h"
#include "fs/fat12.h"
#include "include/types.h"

#define MAX_MACROS      64
#define MACRO_NAME_MAX  31
#define MACRO_VAL_MAX   127
#define INCLUDE_BUF_SZ  4096
#define MAX_IF_DEPTH    16

struct macro {
    char name[MACRO_NAME_MAX + 1];
    char value[MACRO_VAL_MAX + 1];
};

static struct macro macros[MAX_MACROS];
static int num_macros;

static int find_macro(const char *name) {
    for (int i = 0; i < num_macros; i++) {
        if (strcmp(macros[i].name, name) == 0) return i;
    }
    return -1;
}

static void add_macro(const char *name, const char *value) {
    if (num_macros >= MAX_MACROS) return;
    int idx = find_macro(name);
    if (idx >= 0) {
        /* Redefine */
        strcpy(macros[idx].value, value);
        return;
    }
    int i;
    for (i = 0; name[i] && i < MACRO_NAME_MAX; i++)
        macros[num_macros].name[i] = name[i];
    macros[num_macros].name[i] = '\0';
    for (i = 0; value[i] && i < MACRO_VAL_MAX; i++)
        macros[num_macros].value[i] = value[i];
    macros[num_macros].value[i] = '\0';
    num_macros++;
}

static int is_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static int is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

/* Read a word from src at *pos into buf. Returns length. */
static int read_word(const char *s, int len, int *p, char *buf, int buf_max) {
    int n = 0;
    while (*p < len && is_ident_char(s[*p]) && n < buf_max) {
        buf[n++] = s[(*p)++];
    }
    buf[n] = '\0';
    return n;
}

/* Skip spaces (not newlines) */
static void skip_spaces(const char *s, int len, int *p) {
    while (*p < len && (s[*p] == ' ' || s[*p] == '\t')) (*p)++;
}

/* Read rest of line into buf */
static int read_line(const char *s, int len, int *p, char *buf, int buf_max) {
    int n = 0;
    while (*p < len && s[*p] != '\n' && n < buf_max) {
        buf[n++] = s[(*p)++];
    }
    /* Trim trailing whitespace */
    while (n > 0 && (buf[n-1] == ' ' || buf[n-1] == '\t' || buf[n-1] == '\r'))
        n--;
    buf[n] = '\0';
    return n;
}

/* Emit a char to output */
static int emit(char *out, int out_max, int *op, char c) {
    if (*op >= out_max - 1) return -1;
    out[(*op)++] = c;
    return 0;
}

/* Emit a string to output */
static int emit_str(char *out, int out_max, int *op, const char *s) {
    while (*s) {
        if (emit(out, out_max, op, *s++) < 0) return -1;
    }
    return 0;
}

static uint8_t include_buf[INCLUDE_BUF_SZ];

int preprocess(const char *source, int len, char *out, int out_max) {
    num_macros = 0;
    int ip = 0;  /* input position */
    int op = 0;  /* output position */

    /* Conditional compilation stack */
    int if_active[MAX_IF_DEPTH];   /* 1 = currently emitting */
    int if_done[MAX_IF_DEPTH];     /* 1 = already found true branch */
    int if_depth = 0;
    int active = 1;  /* are we in an active code region? */

    while (ip < len) {
        /* Check for preprocessor directive at start of line */
        if (source[ip] == '#') {
            ip++;
            skip_spaces(source, len, &ip);

            char directive[16];
            read_word(source, len, &ip, directive, 15);
            skip_spaces(source, len, &ip);

            if (strcmp(directive, "define") == 0 && active) {
                char name[MACRO_NAME_MAX + 1];
                read_word(source, len, &ip, name, MACRO_NAME_MAX);
                skip_spaces(source, len, &ip);
                char value[MACRO_VAL_MAX + 1];
                read_line(source, len, &ip, value, MACRO_VAL_MAX);
                add_macro(name, value);
            } else if (strcmp(directive, "include") == 0 && active) {
                /* Skip to filename */
                skip_spaces(source, len, &ip);
                if (ip < len && source[ip] == '"') {
                    ip++;
                    char fname[32];
                    int fi = 0;
                    while (ip < len && source[ip] != '"' && fi < 31) {
                        fname[fi++] = source[ip++];
                    }
                    fname[fi] = '\0';
                    if (ip < len) ip++; /* skip closing " */

                    /* Read file from ramdisk */
                    int bytes = fat12_read_file(fname, include_buf, INCLUDE_BUF_SZ - 1);
                    if (bytes > 0) {
                        include_buf[bytes] = '\0';
                        /* Recursively preprocess the included file */
                        char inc_out[INCLUDE_BUF_SZ];
                        int inc_len = preprocess((const char *)include_buf, bytes, inc_out, INCLUDE_BUF_SZ);
                        if (inc_len > 0) {
                            for (int i = 0; i < inc_len; i++) {
                                if (emit(out, out_max, &op, inc_out[i]) < 0) return -1;
                            }
                        }
                    }
                }
            } else if (strcmp(directive, "ifdef") == 0) {
                if (if_depth < MAX_IF_DEPTH) {
                    char name[MACRO_NAME_MAX + 1];
                    read_word(source, len, &ip, name, MACRO_NAME_MAX);
                    int found = find_macro(name) >= 0;
                    if_active[if_depth] = active && found;
                    if_done[if_depth] = active && found;
                    if_depth++;
                    active = if_active[if_depth - 1];
                }
            } else if (strcmp(directive, "ifndef") == 0) {
                if (if_depth < MAX_IF_DEPTH) {
                    char name[MACRO_NAME_MAX + 1];
                    read_word(source, len, &ip, name, MACRO_NAME_MAX);
                    int found = find_macro(name) >= 0;
                    if_active[if_depth] = active && !found;
                    if_done[if_depth] = active && !found;
                    if_depth++;
                    active = if_active[if_depth - 1];
                }
            } else if (strcmp(directive, "else") == 0) {
                if (if_depth > 0) {
                    /* Check parent is active */
                    int parent_active = (if_depth >= 2) ? if_active[if_depth - 2] : 1;
                    if (parent_active && !if_done[if_depth - 1]) {
                        if_active[if_depth - 1] = 1;
                        if_done[if_depth - 1] = 1;
                    } else {
                        if_active[if_depth - 1] = 0;
                    }
                    active = if_active[if_depth - 1];
                }
            } else if (strcmp(directive, "endif") == 0) {
                if (if_depth > 0) {
                    if_depth--;
                    active = (if_depth > 0) ? if_active[if_depth - 1] : 1;
                }
            }

            /* Skip to end of line */
            while (ip < len && source[ip] != '\n') ip++;
            if (ip < len) {
                emit(out, out_max, &op, '\n');
                ip++;
            }
            continue;
        }

        if (!active) {
            /* Skip non-active code */
            while (ip < len && source[ip] != '\n') ip++;
            if (ip < len) {
                emit(out, out_max, &op, '\n');
                ip++;
            }
            continue;
        }

        /* String literals — pass through without macro expansion */
        if (source[ip] == '"') {
            emit(out, out_max, &op, source[ip++]);
            while (ip < len && source[ip] != '"') {
                if (source[ip] == '\\' && ip + 1 < len) {
                    emit(out, out_max, &op, source[ip++]);
                }
                emit(out, out_max, &op, source[ip++]);
            }
            if (ip < len) emit(out, out_max, &op, source[ip++]);
            continue;
        }

        /* Char literals — pass through */
        if (source[ip] == '\'') {
            emit(out, out_max, &op, source[ip++]);
            while (ip < len && source[ip] != '\'') {
                if (source[ip] == '\\' && ip + 1 < len) {
                    emit(out, out_max, &op, source[ip++]);
                }
                emit(out, out_max, &op, source[ip++]);
            }
            if (ip < len) emit(out, out_max, &op, source[ip++]);
            continue;
        }

        /* Identifier — check for macro expansion */
        if (is_ident_start(source[ip])) {
            char word[MACRO_NAME_MAX + 1];
            int wlen = read_word(source, len, &ip, word, MACRO_NAME_MAX);
            int idx = find_macro(word);
            if (idx >= 0) {
                emit_str(out, out_max, &op, macros[idx].value);
            } else {
                for (int i = 0; i < wlen; i++) {
                    emit(out, out_max, &op, word[i]);
                }
            }
            continue;
        }

        /* Everything else: pass through */
        emit(out, out_max, &op, source[ip++]);
    }

    out[op] = '\0';
    return op;
}
