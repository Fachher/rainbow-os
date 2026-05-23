#include "cc/sym.h"
#include "lib/string.h"

static struct symbol globals[SYM_MAX_GLOBAL];
static int num_globals;

static struct symbol locals[SYM_MAX_LOCAL];
static int num_locals;
static int locals_offset;  /* running negative offset from EBP */

void sym_init(void) {
    num_globals = 0;
    num_locals = 0;
    locals_offset = 0;
}

struct symbol *sym_add_global(const char *name, int kind, int data_type) {
    if (num_globals >= SYM_MAX_GLOBAL) return 0;
    struct symbol *s = &globals[num_globals++];
    int i;
    for (i = 0; name[i] && i < SYM_NAME_MAX; i++) s->name[i] = name[i];
    s->name[i] = '\0';
    s->kind = kind;
    s->data_type = data_type;
    s->offset = 0;
    s->size = 0;
    s->defined = 0;
    return s;
}

struct symbol *sym_find_global(const char *name) {
    for (int i = 0; i < num_globals; i++) {
        if (strcmp(globals[i].name, name) == 0) return &globals[i];
    }
    return 0;
}

void sym_begin_locals(void) {
    num_locals = 0;
    locals_offset = 0;
}

struct symbol *sym_add_local(const char *name, int kind, int data_type) {
    if (num_locals >= SYM_MAX_LOCAL) return 0;
    struct symbol *s = &locals[num_locals++];
    int i;
    for (i = 0; name[i] && i < SYM_NAME_MAX; i++) s->name[i] = name[i];
    s->name[i] = '\0';
    s->kind = kind;
    s->data_type = data_type;
    s->size = 0;
    s->defined = 1;

    if (kind == SYM_PARAM) {
        /* Parameters are at positive offsets from EBP:
           [ebp+8] = first param, [ebp+12] = second, etc. */
        s->offset = 8 + (num_locals - 1) * 4;
        /* Recalculate: count only params */
        int param_idx = 0;
        for (int j = 0; j < num_locals; j++) {
            if (locals[j].kind == SYM_PARAM) {
                locals[j].offset = 8 + param_idx * 4;
                param_idx++;
            }
        }
    } else {
        /* Locals at negative offsets from EBP */
        locals_offset += 4;
        s->offset = -locals_offset;
    }
    return s;
}

struct symbol *sym_find_local(const char *name) {
    for (int i = 0; i < num_locals; i++) {
        if (strcmp(locals[i].name, name) == 0) return &locals[i];
    }
    return 0;
}

int sym_locals_size(void) {
    return locals_offset;
}

struct symbol *sym_find(const char *name) {
    struct symbol *s = sym_find_local(name);
    if (s) return s;
    return sym_find_global(name);
}
