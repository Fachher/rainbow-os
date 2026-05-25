#ifndef CC_PARSER_H
#define CC_PARSER_H

/* Parse preprocessed+lexed source and emit code via codegen.
   Call lex_init() before parse_program(). */
void parse_program(void);

/* Returns 1 if the last parse_program() encountered errors */
int parse_had_error(void);

#endif
