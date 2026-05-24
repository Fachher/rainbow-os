#include "buffer.h"
#include "lib/string.h"

static char buf[BUFFER_SIZE];
static uint32_t gap_start;
static uint32_t gap_end;

/* Undo/redo stack */
#define UNDO_STACK_SIZE 2048

enum undo_type { UNDO_INSERT, UNDO_DELETE, UNDO_BOUNDARY };

struct undo_entry {
    uint8_t type;
    uint32_t pos;
    char ch;
};

static struct undo_entry undo_stack[UNDO_STACK_SIZE];
static uint32_t undo_top;       /* next write position (circular) */
static uint32_t undo_count;     /* entries available for undo */
static uint32_t redo_count;     /* entries available for redo */
static bool undo_replaying;     /* suppress recording during undo/redo */

static void undo_push(uint8_t type, uint32_t pos, char ch) {
    if (undo_replaying) return;
    undo_stack[undo_top].type = type;
    undo_stack[undo_top].pos = pos;
    undo_stack[undo_top].ch = ch;
    undo_top = (undo_top + 1) % UNDO_STACK_SIZE;
    if (undo_count < UNDO_STACK_SIZE) undo_count++;
    redo_count = 0;  /* new edit invalidates redo */
}

void buf_init(void) {
    gap_start = 0;
    gap_end = BUFFER_SIZE;
    undo_top = 0;
    undo_count = 0;
    redo_count = 0;
    undo_replaying = false;
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
    undo_push(UNDO_INSERT, gap_start, c);
    buf[gap_start++] = c;
}

void buf_delete_back(void) {
    if (gap_start > 0) {
        undo_push(UNDO_DELETE, gap_start - 1, buf[gap_start - 1]);
        gap_start--;
    }
}

void buf_delete_fwd(void) {
    if (gap_end < BUFFER_SIZE) {
        undo_push(UNDO_DELETE, gap_start, buf[gap_end]);
        gap_end++;
    }
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

/* Undo/redo */

void buf_undo_boundary(void) {
    if (undo_count == 0) return;
    /* Don't push consecutive boundaries */
    uint32_t prev = (undo_top + UNDO_STACK_SIZE - 1) % UNDO_STACK_SIZE;
    if (undo_stack[prev].type == UNDO_BOUNDARY) return;
    undo_push(UNDO_BOUNDARY, 0, 0);
}

bool buf_undo(void) {
    if (undo_count == 0) return false;

    /* Skip trailing boundary */
    uint32_t idx = (undo_top + UNDO_STACK_SIZE - 1) % UNDO_STACK_SIZE;
    if (undo_stack[idx].type == UNDO_BOUNDARY) {
        undo_top = idx;
        undo_count--;
        redo_count++;
        if (undo_count == 0) return false;
        idx = (undo_top + UNDO_STACK_SIZE - 1) % UNDO_STACK_SIZE;
    }

    undo_replaying = true;
    /* Replay entries in reverse until boundary or empty */
    while (undo_count > 0) {
        idx = (undo_top + UNDO_STACK_SIZE - 1) % UNDO_STACK_SIZE;
        struct undo_entry *e = &undo_stack[idx];
        if (e->type == UNDO_BOUNDARY) break;

        undo_top = idx;
        undo_count--;
        redo_count++;

        if (e->type == UNDO_INSERT) {
            /* Undo insert: delete the char at pos */
            buf_move_to(e->pos);
            buf_delete_fwd();
        } else if (e->type == UNDO_DELETE) {
            /* Undo delete: re-insert the char at pos */
            buf_move_to(e->pos);
            buf_insert(e->ch);
        }
    }
    undo_replaying = false;
    return true;
}

bool buf_redo(void) {
    if (redo_count == 0) return false;

    /* Skip leading boundary */
    if (undo_stack[undo_top].type == UNDO_BOUNDARY) {
        undo_top = (undo_top + 1) % UNDO_STACK_SIZE;
        redo_count--;
        undo_count++;
        if (redo_count == 0) return false;
    }

    undo_replaying = true;
    /* Replay entries forward until boundary or empty */
    while (redo_count > 0) {
        struct undo_entry *e = &undo_stack[undo_top];
        if (e->type == UNDO_BOUNDARY) break;

        undo_top = (undo_top + 1) % UNDO_STACK_SIZE;
        redo_count--;
        undo_count++;

        if (e->type == UNDO_INSERT) {
            /* Redo insert: re-insert */
            buf_move_to(e->pos);
            buf_insert(e->ch);
        } else if (e->type == UNDO_DELETE) {
            /* Redo delete: delete again */
            buf_move_to(e->pos);
            buf_delete_fwd();
        }
    }
    undo_replaying = false;
    return true;
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
