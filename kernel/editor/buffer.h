#ifndef BUFFER_H
#define BUFFER_H

#include "include/types.h"

#define BUFFER_SIZE 32768  /* 32 KB max file size */

void     buf_init(void);
void     buf_load(const char *text, uint32_t len);
uint32_t buf_length(void);
void     buf_insert(char c);
void     buf_delete_back(void);
void     buf_delete_fwd(void);
void     buf_move_left(void);
void     buf_move_right(void);
void     buf_move_to(uint32_t pos);
uint32_t buf_cursor_pos(void);
char     buf_char_at(uint32_t pos);
uint32_t buf_get_content(char *out, uint32_t max);

/* Line index */
uint32_t buf_line_count(void);
uint32_t buf_cursor_line(void);
uint32_t buf_cursor_col(void);
uint32_t buf_line_start(uint32_t line);
uint32_t buf_line_length(uint32_t line);

#endif
