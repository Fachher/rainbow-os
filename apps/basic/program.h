#ifndef BASIC_PROGRAM_H
#define BASIC_PROGRAM_H

#include "include/types.h"

/*
 * Program store: flat buffer of tokenized lines sorted by line number.
 * Format: [line_num_lo][line_num_hi][length][tokenized data...]
 * length includes the 3-byte header. End of program: line_num == 0.
 */

#define PROG_SIZE 8192

void     prog_init(void);
void     prog_insert_line(uint16_t num, const uint8_t *tokens, uint8_t len);
void     prog_delete_line(uint16_t num);
uint8_t *prog_find_line(uint16_t num);
uint8_t *prog_first_line(void);
uint8_t *prog_next_line(uint8_t *current);
uint16_t prog_line_number(const uint8_t *line);
uint8_t  prog_line_length(const uint8_t *line);
uint32_t prog_used(void);

#endif
