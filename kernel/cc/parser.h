#ifndef CC_PARSER_H
#define CC_PARSER_H

/* Parse preprocessed+lexed source and emit code via codegen.
   Call lex_init() before parse_program(). */
void parse_program(void);

#endif
