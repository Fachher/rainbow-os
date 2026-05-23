#ifndef CC_LEXER_H
#define CC_LEXER_H

#include "cc/token.h"

#define LEX_IDENT_MAX   63
#define LEX_STR_MAX     255

void  lex_init(const char *source, int length);
int   lex_next(void);
int   lex_peek(void);
int   lex_num_val(void);
char *lex_str_val(void);
int   lex_str_len(void);
int   lex_line(void);

#endif
