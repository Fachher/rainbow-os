#ifndef BASIC_EXPR_H
#define BASIC_EXPR_H

#include "include/types.h"
#include "basic/value.h"

/* String pool for temporary string results */
#define STRING_POOL_SIZE 2048
char *string_pool_alloc(uint8_t len);
void  string_pool_reset(void);

/* Parse and evaluate expression from token stream.
   Advances *pos past consumed tokens.
   Sets error flag on failure. */
struct bas_value expr_eval(const uint8_t *tokens, uint8_t len, uint8_t *pos);

/* Check if there was an expression error */
bool expr_has_error(void);
void expr_clear_error(void);

#endif
