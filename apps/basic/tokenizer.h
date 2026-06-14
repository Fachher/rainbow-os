#ifndef BASIC_TOKENIZER_H
#define BASIC_TOKENIZER_H

#include "include/types.h"

/* Tokenize a source line. Returns number of token bytes written. */
uint8_t tokenize(const char *source, uint8_t *out, uint8_t max_len);

/* Detokenize for LIST: convert tokens back to readable text. */
void detokenize(const uint8_t *tokens, uint8_t len, char *out, uint8_t max_len);

#endif
