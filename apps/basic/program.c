#include "basic/program.h"
#include "lib/string.h"

static uint8_t prog_buf[PROG_SIZE];
static uint32_t prog_end;  /* offset past last byte used */

void prog_init(void) {
    memset(prog_buf, 0, PROG_SIZE);
    prog_end = 0;
}

uint16_t prog_line_number(const uint8_t *line) {
    return (uint16_t)line[0] | ((uint16_t)line[1] << 8);
}

uint8_t prog_line_length(const uint8_t *line) {
    return line[2];
}

uint8_t *prog_first_line(void) {
    if (prog_end == 0) return NULL;
    return prog_buf;
}

uint8_t *prog_next_line(uint8_t *current) {
    uint8_t len = current[2];
    uint8_t *next = current + len;
    if (next >= prog_buf + prog_end) return NULL;
    return next;
}

uint8_t *prog_find_line(uint16_t num) {
    uint8_t *p = prog_first_line();
    while (p) {
        if (prog_line_number(p) == num) return p;
        if (prog_line_number(p) > num) return NULL;
        p = prog_next_line(p);
    }
    return NULL;
}

/* Find insertion point: returns pointer to first line >= num */
static uint8_t *find_insert_pos(uint16_t num) {
    uint8_t *p = prog_first_line();
    while (p) {
        if (prog_line_number(p) >= num) return p;
        p = prog_next_line(p);
    }
    return prog_buf + prog_end;
}

void prog_delete_line(uint16_t num) {
    uint8_t *p = prog_find_line(num);
    if (!p) return;
    uint8_t len = p[2];
    uint8_t *after = p + len;
    uint32_t tail = (uint32_t)(prog_buf + prog_end - after);
    if (tail > 0) memmove(p, after, tail);
    prog_end -= len;
}

void prog_insert_line(uint16_t num, const uint8_t *tokens, uint8_t tok_len) {
    /* Delete existing line with same number first */
    prog_delete_line(num);

    uint8_t total = tok_len + 3;  /* 2 bytes line num + 1 byte length + data */
    if (prog_end + total > PROG_SIZE) return;  /* out of memory */

    /* Find where to insert */
    uint8_t *pos = find_insert_pos(num);
    uint32_t offset = (uint32_t)(pos - prog_buf);

    /* Shift everything after insertion point */
    uint32_t tail = prog_end - offset;
    if (tail > 0) memmove(pos + total, pos, tail);

    /* Write the line header + tokens */
    pos[0] = (uint8_t)(num & 0xFF);
    pos[1] = (uint8_t)(num >> 8);
    pos[2] = total;
    memcpy(pos + 3, tokens, tok_len);

    prog_end += total;
}

uint32_t prog_used(void) {
    return prog_end;
}
