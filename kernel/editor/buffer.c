#include "buffer.h"
#include "lib/string.h"

static char buf[BUFFER_SIZE];
static uint32_t gap_start;
static uint32_t gap_end;

void buf_init(void) {
    gap_start = 0;
    gap_end = BUFFER_SIZE;
}

void buf_load(const char *text, uint32_t len) {
    buf_init();
    if (len > BUFFER_SIZE) len = BUFFER_SIZE;
    memcpy(buf, text, len);
    gap_start = len;
}

uint32_t buf_length(void) {
    return BUFFER_SIZE - (gap_end - gap_start);
}

void buf_insert(char c) {
    if (gap_start >= gap_end) return;  /* buffer full */
    buf[gap_start++] = c;
}

void buf_delete_back(void) {
    if (gap_start > 0) gap_start--;
}

void buf_delete_fwd(void) {
    if (gap_end < BUFFER_SIZE) gap_end++;
}

void buf_move_left(void) {
    if (gap_start > 0) {
        gap_end--;
        buf[gap_end] = buf[gap_start - 1];
        gap_start--;
    }
}

void buf_move_right(void) {
    if (gap_end < BUFFER_SIZE) {
        buf[gap_start] = buf[gap_end];
        gap_start++;
        gap_end++;
    }
}

void buf_move_to(uint32_t pos) {
    uint32_t cur = buf_cursor_pos();
    while (cur > pos && gap_start > 0) {
        buf_move_left();
        cur--;
    }
    while (cur < pos && gap_end < BUFFER_SIZE) {
        buf_move_right();
        cur++;
    }
}

uint32_t buf_cursor_pos(void) {
    return gap_start;
}

char buf_char_at(uint32_t pos) {
    if (pos >= buf_length()) return '\0';
    if (pos < gap_start) return buf[pos];
    return buf[gap_end + (pos - gap_start)];
}

uint32_t buf_get_content(char *out, uint32_t max) {
    uint32_t before = gap_start;
    uint32_t after = BUFFER_SIZE - gap_end;
    uint32_t total = before + after;
    if (total > max) total = max;

    uint32_t copied = 0;
    if (before > 0) {
        uint32_t n = before < max ? before : max;
        memcpy(out, buf, n);
        copied = n;
    }
    if (copied < total && after > 0) {
        uint32_t n = total - copied;
        memcpy(out + copied, buf + gap_end, n);
        copied += n;
    }
    return copied;
}

/* Line index functions */

uint32_t buf_line_count(void) {
    uint32_t lines = 1;
    uint32_t len = buf_length();
    for (uint32_t i = 0; i < len; i++) {
        if (buf_char_at(i) == '\n') lines++;
    }
    return lines;
}

uint32_t buf_cursor_line(void) {
    uint32_t line = 0;
    uint32_t pos = buf_cursor_pos();
    for (uint32_t i = 0; i < pos; i++) {
        if (buf_char_at(i) == '\n') line++;
    }
    return line;
}

uint32_t buf_cursor_col(void) {
    uint32_t pos = buf_cursor_pos();
    uint32_t col = 0;
    for (uint32_t i = pos; i > 0; i--) {
        if (buf_char_at(i - 1) == '\n') break;
        col++;
    }
    return col;
}

uint32_t buf_line_start(uint32_t line) {
    if (line == 0) return 0;
    uint32_t cur_line = 0;
    uint32_t len = buf_length();
    for (uint32_t i = 0; i < len; i++) {
        if (buf_char_at(i) == '\n') {
            cur_line++;
            if (cur_line == line) return i + 1;
        }
    }
    return len;  /* past end */
}

uint32_t buf_line_length(uint32_t line) {
    uint32_t start = buf_line_start(line);
    uint32_t len = buf_length();
    uint32_t i = start;
    while (i < len && buf_char_at(i) != '\n') i++;
    return i - start;
}
