#ifndef CC_SYM_H
#define CC_SYM_H

#define SYM_MAX_GLOBAL  128
#define SYM_MAX_LOCAL   32
#define SYM_NAME_MAX    23

/* Symbol kinds */
#define SYM_GLOBAL_VAR  0
#define SYM_LOCAL_VAR   1
#define SYM_FUNCTION    2
#define SYM_PARAM       3

/* Data types */
#define DT_INT          0
#define DT_CHAR         1
#define DT_VOID         2
#define DT_PTR_INT      3
#define DT_PTR_CHAR     4

struct symbol {
    char name[SYM_NAME_MAX + 1];
    int  kind;
    int  data_type;
    int  offset;      /* stack offset (locals/params) or code address (functions) */
    int  size;        /* array size or 0 for scalar */
    int  defined;     /* 1 = function has been defined (body emitted) */
};

void sym_init(void);

/* Global symbols */
struct symbol *sym_add_global(const char *name, int kind, int data_type);
struct symbol *sym_find_global(const char *name);

/* Local symbols (per-function) */
void sym_begin_locals(void);
struct symbol *sym_add_local(const char *name, int kind, int data_type);
struct symbol *sym_find_local(const char *name);
int  sym_locals_size(void);  /* total bytes of locals on stack */

/* Lookup: local first, then global */
struct symbol *sym_find(const char *name);

#endif
