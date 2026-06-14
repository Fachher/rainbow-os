#include "editor.h"
#include "buffer.h"
#include "view.h"
#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "fs/fat12.h"
#include "fs/diskfs.h"
#include "lib/string.h"
#include "drivers/serial.h"

enum editor_mode { MODE_NORMAL, MODE_INSERT, MODE_COMMAND, MODE_VISUAL, MODE_VISUAL_LINE };
enum pending_op  { OP_NONE, OP_DELETE, OP_YANK, OP_CHANGE };

static struct editor_view view;
static enum editor_mode mode;
static char filename[13];
static bool modified;
static char cmd_buf[80];
static uint8_t cmd_len;
static bool running;

/* Two-key command flags */
static bool awaiting_g;
static bool awaiting_r;
static bool awaiting_gt;   /* for >> */
static bool awaiting_lt;   /* for << */

/* Visual selection */
static uint32_t sel_anchor;

/* Yank buffer */
static char yank_buf[4096];
static uint32_t yank_len;
static bool yank_linewise;

/* Repeat count */
static uint32_t repeat_count;
static bool count_active;

/* Operator-pending */
static enum pending_op pending_operator;
static uint32_t op_count;  /* count captured before operator key */

/* ---- Helpers ---- */

static const char *mode_string(void) {
    switch (mode) {
        case MODE_NORMAL:      return "NORMAL";
        case MODE_INSERT:      return "INSERT";
        case MODE_COMMAND:     return "COMMAND";
        case MODE_VISUAL:      return "VISUAL";
        case MODE_VISUAL_LINE: return "V-LINE";
    }
    return "";
}

static void sync_cursor(void) {
    view.cursor_line = buf_cursor_line();
    view.cursor_col = buf_cursor_col();
    view_scroll_to_cursor(&view);
}

static void move_to_line_col(uint32_t line, uint32_t col) {
    uint32_t pos = buf_line_start(line);
    uint32_t len = buf_line_length(line);
    if (col > len) col = len;
    buf_move_to(pos + col);
    sync_cursor();
}

static void move_vertical(int delta) {
    uint32_t cur_line = buf_cursor_line();
    uint32_t cur_col = buf_cursor_col();
    uint32_t total = buf_line_count();

    int new_line = (int)cur_line + delta;
    if (new_line < 0) new_line = 0;
    if ((uint32_t)new_line >= total) new_line = (int)total - 1;

    move_to_line_col((uint32_t)new_line, cur_col);
}

static void set_status(const char *msg) {
    uint8_t i;
    for (i = 0; msg[i]; i++) cmd_buf[i] = msg[i];
    cmd_buf[i] = '\0';
}

/* ---- Word motion helpers ---- */

static bool is_word_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static bool is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* w: move to next word start */
static uint32_t word_forward_pos(uint32_t pos) {
    uint32_t len = buf_length();
    if (pos >= len) return len;

    char c = buf_char_at(pos);
    if (is_word_char(c)) {
        while (pos < len && is_word_char(buf_char_at(pos))) pos++;
    } else if (!is_whitespace(c)) {
        while (pos < len && !is_word_char(buf_char_at(pos)) && !is_whitespace(buf_char_at(pos))) pos++;
    }
    while (pos < len && is_whitespace(buf_char_at(pos))) pos++;
    return pos;
}

/* W: move to next WORD start (whitespace-delimited) */
static uint32_t word_forward_big_pos(uint32_t pos) {
    uint32_t len = buf_length();
    if (pos >= len) return len;

    while (pos < len && !is_whitespace(buf_char_at(pos))) pos++;
    while (pos < len && is_whitespace(buf_char_at(pos))) pos++;
    return pos;
}

/* b: move to previous word start */
static uint32_t word_backward_pos(uint32_t pos) {
    if (pos == 0) return 0;
    pos--;

    while (pos > 0 && is_whitespace(buf_char_at(pos))) pos--;

    if (pos == 0 && is_whitespace(buf_char_at(pos))) return 0;

    char c = buf_char_at(pos);
    if (is_word_char(c)) {
        while (pos > 0 && is_word_char(buf_char_at(pos - 1))) pos--;
    } else {
        while (pos > 0 && !is_word_char(buf_char_at(pos - 1)) && !is_whitespace(buf_char_at(pos - 1))) pos--;
    }
    return pos;
}

/* B: move to previous WORD start */
static uint32_t word_backward_big_pos(uint32_t pos) {
    if (pos == 0) return 0;
    pos--;

    while (pos > 0 && is_whitespace(buf_char_at(pos))) pos--;

    if (pos == 0 && is_whitespace(buf_char_at(pos))) return 0;

    while (pos > 0 && !is_whitespace(buf_char_at(pos - 1))) pos--;
    return pos;
}

/* e: move to end of current/next word */
static uint32_t word_end_pos(uint32_t pos) {
    uint32_t len = buf_length();
    if (pos + 1 >= len) return len > 0 ? len - 1 : 0;
    pos++;

    while (pos < len && is_whitespace(buf_char_at(pos))) pos++;

    if (pos >= len) return len > 0 ? len - 1 : 0;

    char c = buf_char_at(pos);
    if (is_word_char(c)) {
        while (pos + 1 < len && is_word_char(buf_char_at(pos + 1))) pos++;
    } else if (!is_whitespace(c)) {
        while (pos + 1 < len && !is_word_char(buf_char_at(pos + 1)) && !is_whitespace(buf_char_at(pos + 1))) pos++;
    }
    return pos;
}

/* E: move to end of current/next WORD */
static uint32_t word_end_big_pos(uint32_t pos) {
    uint32_t len = buf_length();
    if (pos + 1 >= len) return len > 0 ? len - 1 : 0;
    pos++;

    while (pos < len && is_whitespace(buf_char_at(pos))) pos++;

    if (pos >= len) return len > 0 ? len - 1 : 0;

    while (pos + 1 < len && !is_whitespace(buf_char_at(pos + 1))) pos++;
    return pos;
}

/* ---- Yank/delete helpers ---- */

static void yank_range(uint32_t start, uint32_t end) {
    uint32_t len = end - start;
    if (len > sizeof(yank_buf) - 1) len = sizeof(yank_buf) - 1;
    for (uint32_t i = 0; i < len; i++)
        yank_buf[i] = buf_char_at(start + i);
    yank_len = len;
}

static void delete_range(uint32_t start, uint32_t end) {
    buf_move_to(start);
    uint32_t count = end - start;
    for (uint32_t i = 0; i < count; i++)
        buf_delete_fwd();
    sync_cursor();
    modified = true;
}

static void yank_current_line(void) {
    uint32_t line = buf_cursor_line();
    uint32_t start = buf_line_start(line);
    uint32_t len = buf_line_length(line);
    if (len > sizeof(yank_buf) - 1) len = sizeof(yank_buf) - 1;
    for (uint32_t i = 0; i < len; i++)
        yank_buf[i] = buf_char_at(start + i);
    yank_len = len;
}

/* Compute visual selection bounds */
static void get_sel_bounds(uint32_t *start, uint32_t *end) {
    uint32_t cursor = buf_cursor_pos();
    if (cursor <= sel_anchor) {
        *start = cursor;
        *end = sel_anchor + 1;
    } else {
        *start = sel_anchor;
        *end = cursor + 1;
    }
    if (*end > buf_length()) *end = buf_length();

    if (mode == MODE_VISUAL_LINE) {
        uint32_t sline = 0, eline = 0;
        uint32_t len = buf_length();
        uint32_t line = 0;
        for (uint32_t i = 0; i < len; i++) {
            if (i == *start) sline = line;
            if (i < *end) eline = line;
            if (buf_char_at(i) == '\n') line++;
        }
        *start = buf_line_start(sline);
        uint32_t estart = buf_line_start(eline);
        uint32_t elen = buf_line_length(eline);
        *end = estart + elen;
        if (*end < buf_length()) (*end)++;
    }
}

/* ---- Operator-pending motion handling ---- */

static void cancel_operator(void) {
    pending_operator = OP_NONE;
    op_count = 0;
}

static void apply_operator_charwise(uint32_t from, uint32_t to) {
    uint32_t start = from < to ? from : to;
    uint32_t end = from < to ? to : from;

    buf_undo_boundary();
    yank_range(start, end);
    yank_linewise = false;

    if (pending_operator == OP_DELETE || pending_operator == OP_CHANGE) {
        delete_range(start, end);
    }
    if (pending_operator == OP_CHANGE) {
        mode = MODE_INSERT;
    }
    cancel_operator();
}

static void apply_operator_linewise(uint32_t from_line, uint32_t to_line) {
    if (from_line > to_line) {
        uint32_t tmp = from_line;
        from_line = to_line;
        to_line = tmp;
    }

    uint32_t start = buf_line_start(from_line);
    uint32_t end_line_start = buf_line_start(to_line);
    uint32_t end_line_len = buf_line_length(to_line);
    uint32_t end = end_line_start + end_line_len;
    if (end < buf_length()) end++;  /* include trailing newline */

    buf_undo_boundary();
    yank_range(start, end);
    yank_linewise = true;

    if (pending_operator == OP_DELETE) {
        delete_range(start, end);
    } else if (pending_operator == OP_CHANGE) {
        /* For cc: delete line contents but keep one empty line */
        uint32_t line_start = buf_line_start(from_line);
        delete_range(start, end);
        buf_move_to(line_start);
        sync_cursor();
        mode = MODE_INSERT;
    }
    cancel_operator();
}

/* Handle motion key when operator is pending */
static void handle_operator_motion(int key, uint32_t count) {
    uint32_t cur = buf_cursor_pos();
    uint32_t cur_line = buf_cursor_line();
    uint32_t total_count = op_count * count;

    /* Doubled operator → linewise on current line(s) */
    if ((key == 'd' && pending_operator == OP_DELETE) ||
        (key == 'y' && pending_operator == OP_YANK) ||
        (key == 'c' && pending_operator == OP_CHANGE)) {
        uint32_t end_line = cur_line + total_count - 1;
        uint32_t max_line = buf_line_count() - 1;
        if (end_line > max_line) end_line = max_line;
        apply_operator_linewise(cur_line, end_line);
        return;
    }

    /* Charwise motions */
    switch (key) {
        case 'w': {
            uint32_t target = cur;
            for (uint32_t i = 0; i < total_count; i++)
                target = word_forward_pos(target);
            apply_operator_charwise(cur, target);
            return;
        }
        case 'W': {
            uint32_t target = cur;
            for (uint32_t i = 0; i < total_count; i++)
                target = word_forward_big_pos(target);
            apply_operator_charwise(cur, target);
            return;
        }
        case 'b': {
            uint32_t target = cur;
            for (uint32_t i = 0; i < total_count; i++)
                target = word_backward_pos(target);
            apply_operator_charwise(target, cur);
            return;
        }
        case 'B': {
            uint32_t target = cur;
            for (uint32_t i = 0; i < total_count; i++)
                target = word_backward_big_pos(target);
            apply_operator_charwise(target, cur);
            return;
        }
        case 'e': {
            uint32_t target = cur;
            for (uint32_t i = 0; i < total_count; i++)
                target = word_end_pos(target);
            apply_operator_charwise(cur, target + 1);
            return;
        }
        case 'E': {
            uint32_t target = cur;
            for (uint32_t i = 0; i < total_count; i++)
                target = word_end_big_pos(target);
            apply_operator_charwise(cur, target + 1);
            return;
        }
        case 'h': case KEY_LEFT: {
            uint32_t target = cur;
            for (uint32_t i = 0; i < total_count && target > 0; i++) target--;
            apply_operator_charwise(target, cur);
            return;
        }
        case 'l': case KEY_RIGHT: {
            uint32_t target = cur;
            uint32_t len = buf_length();
            for (uint32_t i = 0; i < total_count && target < len; i++) target++;
            apply_operator_charwise(cur, target);
            return;
        }
        case '0': case KEY_HOME: {
            uint32_t line_start = buf_line_start(cur_line);
            apply_operator_charwise(line_start, cur);
            return;
        }
        case '$': case KEY_END: {
            uint32_t start = buf_line_start(cur_line);
            uint32_t len = buf_line_length(cur_line);
            apply_operator_charwise(cur, start + len);
            return;
        }

        /* Linewise motions */
        case 'j': case KEY_DOWN: {
            uint32_t target_line = cur_line + total_count;
            uint32_t max_line = buf_line_count() - 1;
            if (target_line > max_line) target_line = max_line;
            apply_operator_linewise(cur_line, target_line);
            return;
        }
        case 'k': case KEY_UP: {
            uint32_t target_line = total_count > cur_line ? 0 : cur_line - total_count;
            apply_operator_linewise(target_line, cur_line);
            return;
        }
        case 'G': {
            uint32_t last_line = buf_line_count() - 1;
            apply_operator_linewise(cur_line, last_line);
            return;
        }
        case 'g':
            awaiting_g = true;
            return;
        default:
            break;
    }

    /* Unknown motion — cancel operator */
    cancel_operator();
}

/* ---- Save ---- */

static int do_save(const char *save_name) {
    if (!save_name || !save_name[0]) {
        set_status("E32: No file name");
        return -1;
    }
    static char save_buf[BUFFER_SIZE];
    uint32_t len = buf_get_content(save_buf, BUFFER_SIZE);
    int result = diskfs_write_file(save_name, (const uint8_t *)save_buf, len);
    if (result != 0) {
        set_status("E: Write failed");
        return -1;
    }
    if (save_name != filename) {
        uint8_t i;
        for (i = 0; save_name[i] && i < 12; i++)
            filename[i] = save_name[i];
        filename[i] = '\0';
    }
    modified = false;
    set_status("Written");
    return 0;
}

/* ---- Normal Mode ---- */

static void handle_normal(int key) {
    /* 1. Count accumulation */
    if ((key >= '1' && key <= '9') || (key == '0' && count_active)) {
        repeat_count = repeat_count * 10 + (uint32_t)(key - '0');
        if (repeat_count > 9999) repeat_count = 9999;
        count_active = true;
        return;
    }

    /* 2. Awaiting single-char input */
    if (awaiting_r) {
        awaiting_r = false;
        uint32_t cnt = repeat_count > 0 ? repeat_count : 1;
        repeat_count = 0; count_active = false;
        if (key >= ' ' && key < 127) {
            buf_undo_boundary();
            uint32_t pos = buf_cursor_pos();
            uint32_t len = buf_length();
            for (uint32_t i = 0; i < cnt && pos + i < len; i++) {
                buf_move_to(pos + i);
                buf_delete_fwd();
                buf_insert((char)key);
            }
            /* Stay on last replaced char */
            buf_move_to(pos + cnt - 1);
            sync_cursor();
            modified = true;
        }
        return;
    }

    /* 3. Two-key commands */
    if (awaiting_g) {
        awaiting_g = false;
        if (key == 'g') {
            if (pending_operator != OP_NONE) {
                /* dgg, ygg, cgg — operate from cursor to file start */
                uint32_t cur_line = buf_cursor_line();
                apply_operator_linewise(0, cur_line);
            } else {
                uint32_t cnt = repeat_count > 0 ? repeat_count : 0;
                repeat_count = 0; count_active = false;
                if (cnt > 0) {
                    /* Ngg: go to line N (1-indexed) */
                    uint32_t target = cnt - 1;
                    uint32_t max_line = buf_line_count() - 1;
                    if (target > max_line) target = max_line;
                    move_to_line_col(target, 0);
                } else {
                    move_to_line_col(0, 0);
                }
            }
        }
        repeat_count = 0; count_active = false;
        return;
    }
    if (awaiting_gt) {
        awaiting_gt = false;
        if (key == '>') {
            uint32_t cnt = repeat_count > 0 ? repeat_count : 1;
            repeat_count = 0; count_active = false;
            buf_undo_boundary();
            uint32_t cur_line = buf_cursor_line();
            for (uint32_t n = 0; n < cnt; n++) {
                uint32_t line = cur_line + n;
                if (line >= buf_line_count()) break;
                uint32_t start = buf_line_start(line);
                buf_move_to(start);
                buf_insert(' '); buf_insert(' ');
                buf_insert(' '); buf_insert(' ');
            }
            sync_cursor();
            modified = true;
        }
        repeat_count = 0; count_active = false;
        return;
    }
    if (awaiting_lt) {
        awaiting_lt = false;
        if (key == '<') {
            uint32_t cnt = repeat_count > 0 ? repeat_count : 1;
            repeat_count = 0; count_active = false;
            buf_undo_boundary();
            uint32_t cur_line = buf_cursor_line();
            for (uint32_t n = 0; n < cnt; n++) {
                uint32_t line = cur_line + n;
                if (line >= buf_line_count()) break;
                uint32_t start = buf_line_start(line);
                uint32_t llen = buf_line_length(line);
                buf_move_to(start);
                uint32_t spaces = 0;
                while (spaces < 4 && spaces < llen && buf_char_at(start + spaces) == ' ')
                    spaces++;
                for (uint32_t i = 0; i < spaces; i++) buf_delete_fwd();
            }
            sync_cursor();
            modified = true;
        }
        repeat_count = 0; count_active = false;
        return;
    }

    /* Resolve count */
    uint32_t count = repeat_count > 0 ? repeat_count : 1;
    repeat_count = 0;
    count_active = false;

    /* 4. Operator-pending mode: handle motion */
    if (pending_operator != OP_NONE) {
        handle_operator_motion(key, count);
        return;
    }

    /* 5. Regular commands */
    switch (key) {
        /* Movement */
        case 'h': case KEY_LEFT:
            for (uint32_t i = 0; i < count; i++) buf_move_left();
            sync_cursor();
            break;
        case 'l': case KEY_RIGHT:
            for (uint32_t i = 0; i < count; i++) buf_move_right();
            sync_cursor();
            break;
        case 'j': case KEY_DOWN:
            move_vertical((int)count);
            break;
        case 'k': case KEY_UP:
            move_vertical(-(int)count);
            break;
        case '0': case KEY_HOME: {
            uint32_t line = buf_cursor_line();
            buf_move_to(buf_line_start(line));
            sync_cursor();
            break;
        }
        case '$': case KEY_END: {
            uint32_t line = buf_cursor_line();
            uint32_t start = buf_line_start(line);
            uint32_t len = buf_line_length(line);
            uint32_t end = start + len;
            if (len > 0) end--;
            buf_move_to(end);
            sync_cursor();
            break;
        }
        case '^': {
            /* First non-blank character */
            uint32_t line = buf_cursor_line();
            uint32_t start = buf_line_start(line);
            uint32_t len = buf_line_length(line);
            uint32_t pos = start;
            while (pos < start + len && (buf_char_at(pos) == ' ' || buf_char_at(pos) == '\t'))
                pos++;
            buf_move_to(pos);
            sync_cursor();
            break;
        }

        /* Word motions */
        case 'w': {
            uint32_t pos = buf_cursor_pos();
            for (uint32_t i = 0; i < count; i++)
                pos = word_forward_pos(pos);
            buf_move_to(pos);
            sync_cursor();
            break;
        }
        case 'W': {
            uint32_t pos = buf_cursor_pos();
            for (uint32_t i = 0; i < count; i++)
                pos = word_forward_big_pos(pos);
            buf_move_to(pos);
            sync_cursor();
            break;
        }
        case 'b': {
            uint32_t pos = buf_cursor_pos();
            for (uint32_t i = 0; i < count; i++)
                pos = word_backward_pos(pos);
            buf_move_to(pos);
            sync_cursor();
            break;
        }
        case 'B': {
            uint32_t pos = buf_cursor_pos();
            for (uint32_t i = 0; i < count; i++)
                pos = word_backward_big_pos(pos);
            buf_move_to(pos);
            sync_cursor();
            break;
        }
        case 'e': {
            uint32_t pos = buf_cursor_pos();
            for (uint32_t i = 0; i < count; i++)
                pos = word_end_pos(pos);
            buf_move_to(pos);
            sync_cursor();
            break;
        }
        case 'E': {
            uint32_t pos = buf_cursor_pos();
            for (uint32_t i = 0; i < count; i++)
                pos = word_end_big_pos(pos);
            buf_move_to(pos);
            sync_cursor();
            break;
        }

        case 'g':
            awaiting_g = true;
            break;
        case 'G': {
            if (count > 1) {
                /* NG: go to line N */
                uint32_t target = count - 1;
                uint32_t max_line = buf_line_count() - 1;
                if (target > max_line) target = max_line;
                move_to_line_col(target, 0);
            } else {
                uint32_t last_line = buf_line_count() - 1;
                move_to_line_col(last_line, 0);
            }
            break;
        }

        /* Insert mode entry */
        case 'i':
            buf_undo_boundary();
            mode = MODE_INSERT;
            break;
        case 'a':
            buf_undo_boundary();
            buf_move_right();
            sync_cursor();
            mode = MODE_INSERT;
            break;
        case 'A': {
            /* Append at end of line */
            buf_undo_boundary();
            uint32_t line = buf_cursor_line();
            uint32_t start = buf_line_start(line);
            uint32_t len = buf_line_length(line);
            buf_move_to(start + len);
            sync_cursor();
            mode = MODE_INSERT;
            break;
        }
        case 'I': {
            /* Insert at first non-blank */
            buf_undo_boundary();
            uint32_t line = buf_cursor_line();
            uint32_t start = buf_line_start(line);
            uint32_t len = buf_line_length(line);
            uint32_t pos = start;
            while (pos < start + len && (buf_char_at(pos) == ' ' || buf_char_at(pos) == '\t'))
                pos++;
            buf_move_to(pos);
            sync_cursor();
            mode = MODE_INSERT;
            break;
        }
        case 'o': {
            buf_undo_boundary();
            uint32_t line = buf_cursor_line();
            uint32_t start = buf_line_start(line);
            uint32_t len = buf_line_length(line);
            buf_move_to(start + len);
            buf_insert('\n');
            sync_cursor();
            modified = true;
            mode = MODE_INSERT;
            break;
        }
        case 'O': {
            buf_undo_boundary();
            uint32_t line = buf_cursor_line();
            uint32_t start = buf_line_start(line);
            buf_move_to(start);
            buf_insert('\n');
            buf_move_left();
            sync_cursor();
            modified = true;
            mode = MODE_INSERT;
            break;
        }

        /* Delete/yank single-key commands */
        case 'x': case KEY_DELETE: {
            buf_undo_boundary();
            for (uint32_t i = 0; i < count; i++) {
                if (buf_cursor_pos() < buf_length())
                    buf_delete_fwd();
            }
            sync_cursor();
            modified = true;
            break;
        }
        case 'r':
            awaiting_r = true;
            /* count is stored in repeat_count which was already resolved,
               re-store it for use in awaiting_r handler */
            repeat_count = count;
            count_active = true;
            break;

        /* Operators */
        case 'd':
            pending_operator = OP_DELETE;
            op_count = count;
            break;
        case 'y':
            pending_operator = OP_YANK;
            op_count = count;
            break;
        case 'c':
            pending_operator = OP_CHANGE;
            op_count = count;
            break;

        /* Aliases */
        case 'D': {
            /* Delete to end of line */
            buf_undo_boundary();
            uint32_t line = buf_cursor_line();
            uint32_t start = buf_line_start(line);
            uint32_t len = buf_line_length(line);
            uint32_t cur = buf_cursor_pos();
            uint32_t end = start + len;
            if (cur < end) {
                yank_range(cur, end);
                yank_linewise = false;
                delete_range(cur, end);
            }
            break;
        }
        case 'C': {
            /* Change to end of line */
            buf_undo_boundary();
            uint32_t line = buf_cursor_line();
            uint32_t start = buf_line_start(line);
            uint32_t len = buf_line_length(line);
            uint32_t cur = buf_cursor_pos();
            uint32_t end = start + len;
            if (cur < end) {
                yank_range(cur, end);
                yank_linewise = false;
                delete_range(cur, end);
            }
            mode = MODE_INSERT;
            break;
        }
        case 'Y': {
            /* Yank line (vim default) */
            for (uint32_t i = 0; i < count; i++)
                yank_current_line();
            yank_linewise = true;
            break;
        }
        case 'S': {
            /* Substitute line: delete contents, enter insert */
            buf_undo_boundary();
            uint32_t line = buf_cursor_line();
            uint32_t start = buf_line_start(line);
            uint32_t len = buf_line_length(line);
            if (len > 0) {
                yank_range(start, start + len);
                yank_linewise = false;
                delete_range(start, start + len);
            }
            mode = MODE_INSERT;
            break;
        }

        case 'p': {
            if (yank_len == 0) break;
            buf_undo_boundary();
            for (uint32_t n = 0; n < count; n++) {
                if (yank_linewise) {
                    uint32_t line = buf_cursor_line();
                    uint32_t start = buf_line_start(line);
                    uint32_t len = buf_line_length(line);
                    buf_move_to(start + len);
                    buf_insert('\n');
                    for (uint32_t i = 0; i < yank_len; i++)
                        buf_insert(yank_buf[i]);
                } else {
                    buf_move_right();
                    for (uint32_t i = 0; i < yank_len; i++)
                        buf_insert(yank_buf[i]);
                }
            }
            sync_cursor();
            modified = true;
            break;
        }

        /* Misc editing */
        case 'J': {
            /* Join lines */
            buf_undo_boundary();
            for (uint32_t n = 0; n < count; n++) {
                uint32_t line = buf_cursor_line();
                if (line + 1 >= buf_line_count()) break;
                uint32_t start = buf_line_start(line);
                uint32_t len = buf_line_length(line);
                uint32_t nl_pos = start + len;
                if (nl_pos < buf_length() && buf_char_at(nl_pos) == '\n') {
                    buf_move_to(nl_pos);
                    buf_delete_fwd();
                    /* Remove leading whitespace on joined line */
                    while (buf_cursor_pos() < buf_length() &&
                           (buf_char_at(buf_cursor_pos()) == ' ' || buf_char_at(buf_cursor_pos()) == '\t'))
                        buf_delete_fwd();
                    /* Insert single space */
                    buf_insert(' ');
                    modified = true;
                }
            }
            sync_cursor();
            break;
        }
        case '~': {
            /* Toggle case */
            buf_undo_boundary();
            for (uint32_t i = 0; i < count; i++) {
                uint32_t pos = buf_cursor_pos();
                if (pos >= buf_length()) break;
                char c = buf_char_at(pos);
                char toggled = c;
                if (c >= 'a' && c <= 'z') toggled = c - 32;
                else if (c >= 'A' && c <= 'Z') toggled = c + 32;
                if (toggled != c) {
                    buf_delete_fwd();
                    buf_insert(toggled);
                    modified = true;
                } else {
                    buf_move_right();
                }
            }
            sync_cursor();
            break;
        }
        case '>':
            awaiting_gt = true;
            repeat_count = count;
            count_active = true;
            break;
        case '<':
            awaiting_lt = true;
            repeat_count = count;
            count_active = true;
            break;

        /* Visual mode */
        case 'v':
            sel_anchor = buf_cursor_pos();
            mode = MODE_VISUAL;
            break;
        case 'V':
            sel_anchor = buf_cursor_pos();
            mode = MODE_VISUAL_LINE;
            break;

        /* Undo/redo */
        case 'u':
            buf_undo_boundary();
            for (uint32_t i = 0; i < count; i++) {
                if (!buf_undo()) break;
            }
            sync_cursor();
            modified = true;
            break;
        case KEY_CTRL('r'):
            for (uint32_t i = 0; i < count; i++) {
                if (!buf_redo()) break;
            }
            sync_cursor();
            modified = true;
            break;

        /* Command mode */
        case ':':
            mode = MODE_COMMAND;
            cmd_len = 0;
            cmd_buf[0] = ':';
            cmd_buf[1] = '\0';
            cmd_len = 1;
            break;

        /* Scrolling */
        case KEY_CTRL('u'): case KEY_PGUP:
            move_vertical(-(int)(view.screen_rows / 2) * (int)count);
            break;
        case KEY_CTRL('d'): case KEY_PGDN:
            move_vertical((int)(view.screen_rows / 2) * (int)count);
            break;
    }
}

/* ---- Insert Mode ---- */

static void handle_insert(int key) {
    switch (key) {
        case KEY_ESCAPE:
            buf_undo_boundary();
            mode = MODE_NORMAL;
            if (buf_cursor_col() > 0) buf_move_left();
            sync_cursor();
            break;
        case '\b':
            if (buf_cursor_pos() > 0) {
                buf_delete_back();
                sync_cursor();
                modified = true;
            }
            break;
        case KEY_DELETE:
            buf_delete_fwd();
            sync_cursor();
            modified = true;
            break;
        case '\n':
            buf_insert('\n');
            sync_cursor();
            modified = true;
            break;
        case '\t':
            buf_insert(' '); buf_insert(' ');
            buf_insert(' '); buf_insert(' ');
            sync_cursor();
            modified = true;
            break;
        case KEY_LEFT:
            buf_move_left();
            sync_cursor();
            break;
        case KEY_RIGHT:
            buf_move_right();
            sync_cursor();
            break;
        case KEY_UP:
            move_vertical(-1);
            break;
        case KEY_DOWN:
            move_vertical(1);
            break;
        default:
            if (key >= ' ' && key < 127) {
                buf_insert((char)key);
                sync_cursor();
                modified = true;
            }
            break;
    }
}

/* ---- Visual Mode ---- */

static void handle_visual(int key) {
    if (awaiting_g) {
        awaiting_g = false;
        if (key == 'g') move_to_line_col(0, 0);
        return;
    }

    switch (key) {
        case KEY_ESCAPE:
            mode = MODE_NORMAL;
            break;
        case 'v':
            if (mode == MODE_VISUAL) mode = MODE_NORMAL;
            else mode = MODE_VISUAL;
            break;
        case 'V':
            if (mode == MODE_VISUAL_LINE) mode = MODE_NORMAL;
            else mode = MODE_VISUAL_LINE;
            break;

        /* Movement */
        case 'h': case KEY_LEFT:
            buf_move_left();
            sync_cursor();
            break;
        case 'l': case KEY_RIGHT:
            buf_move_right();
            sync_cursor();
            break;
        case 'j': case KEY_DOWN:
            move_vertical(1);
            break;
        case 'k': case KEY_UP:
            move_vertical(-1);
            break;
        case '0': case KEY_HOME: {
            uint32_t line = buf_cursor_line();
            buf_move_to(buf_line_start(line));
            sync_cursor();
            break;
        }
        case '$': case KEY_END: {
            uint32_t line = buf_cursor_line();
            uint32_t start = buf_line_start(line);
            uint32_t len = buf_line_length(line);
            uint32_t end = start + len;
            if (len > 0) end--;
            buf_move_to(end);
            sync_cursor();
            break;
        }
        case 'w': {
            uint32_t pos = word_forward_pos(buf_cursor_pos());
            buf_move_to(pos);
            sync_cursor();
            break;
        }
        case 'W': {
            uint32_t pos = word_forward_big_pos(buf_cursor_pos());
            buf_move_to(pos);
            sync_cursor();
            break;
        }
        case 'b': {
            uint32_t pos = word_backward_pos(buf_cursor_pos());
            buf_move_to(pos);
            sync_cursor();
            break;
        }
        case 'B': {
            uint32_t pos = word_backward_big_pos(buf_cursor_pos());
            buf_move_to(pos);
            sync_cursor();
            break;
        }
        case 'e': {
            uint32_t pos = word_end_pos(buf_cursor_pos());
            buf_move_to(pos);
            sync_cursor();
            break;
        }
        case 'E': {
            uint32_t pos = word_end_big_pos(buf_cursor_pos());
            buf_move_to(pos);
            sync_cursor();
            break;
        }
        case 'g':
            awaiting_g = true;
            break;
        case 'G': {
            uint32_t last_line = buf_line_count() - 1;
            move_to_line_col(last_line, 0);
            break;
        }
        case KEY_CTRL('u'): case KEY_PGUP:
            move_vertical(-(int)(view.screen_rows / 2));
            break;
        case KEY_CTRL('d'): case KEY_PGDN:
            move_vertical((int)(view.screen_rows / 2));
            break;

        /* Actions */
        case 'y': {
            uint32_t s, e;
            get_sel_bounds(&s, &e);
            yank_range(s, e);
            yank_linewise = (mode == MODE_VISUAL_LINE);
            mode = MODE_NORMAL;
            break;
        }
        case 'd': case 'x': {
            buf_undo_boundary();
            uint32_t s, e;
            get_sel_bounds(&s, &e);
            yank_range(s, e);
            yank_linewise = (mode == MODE_VISUAL_LINE);
            delete_range(s, e);
            mode = MODE_NORMAL;
            break;
        }
        case 'c': {
            buf_undo_boundary();
            uint32_t s, e;
            get_sel_bounds(&s, &e);
            yank_range(s, e);
            yank_linewise = (mode == MODE_VISUAL_LINE);
            delete_range(s, e);
            mode = MODE_INSERT;
            break;
        }
        case '>': {
            /* Indent selected lines */
            buf_undo_boundary();
            uint32_t s, e;
            get_sel_bounds(&s, &e);
            uint32_t sline = 0, eline = 0, line = 0;
            uint32_t len = buf_length();
            for (uint32_t i = 0; i < len; i++) {
                if (i == s) sline = line;
                if (i < e) eline = line;
                if (buf_char_at(i) == '\n') line++;
            }
            for (uint32_t l = sline; l <= eline; l++) {
                uint32_t start = buf_line_start(l);
                buf_move_to(start);
                buf_insert(' '); buf_insert(' ');
                buf_insert(' '); buf_insert(' ');
            }
            sync_cursor();
            modified = true;
            mode = MODE_NORMAL;
            break;
        }
        case '<': {
            /* Outdent selected lines */
            buf_undo_boundary();
            uint32_t s, e;
            get_sel_bounds(&s, &e);
            uint32_t sline = 0, eline = 0, line = 0;
            uint32_t len = buf_length();
            for (uint32_t i = 0; i < len; i++) {
                if (i == s) sline = line;
                if (i < e) eline = line;
                if (buf_char_at(i) == '\n') line++;
            }
            for (uint32_t l = sline; l <= eline; l++) {
                uint32_t start = buf_line_start(l);
                uint32_t llen = buf_line_length(l);
                buf_move_to(start);
                uint32_t spaces = 0;
                while (spaces < 4 && spaces < llen && buf_char_at(start + spaces) == ' ')
                    spaces++;
                for (uint32_t i = 0; i < spaces; i++) buf_delete_fwd();
            }
            sync_cursor();
            modified = true;
            mode = MODE_NORMAL;
            break;
        }
        case '~': {
            /* Toggle case of selection */
            buf_undo_boundary();
            uint32_t s, e;
            get_sel_bounds(&s, &e);
            for (uint32_t i = s; i < e; i++) {
                char ch = buf_char_at(i);
                char toggled = ch;
                if (ch >= 'a' && ch <= 'z') toggled = ch - 32;
                else if (ch >= 'A' && ch <= 'Z') toggled = ch + 32;
                if (toggled != ch) {
                    buf_move_to(i);
                    buf_delete_fwd();
                    buf_insert(toggled);
                }
            }
            sync_cursor();
            modified = true;
            mode = MODE_NORMAL;
            break;
        }
    }
}

/* ---- Command Mode ---- */

static void cmd_execute(void) {
    char *cmd = cmd_buf + 1;

    if (strcmp(cmd, "q") == 0) {
        if (modified) {
            set_status("E37: No write since last change (add ! to override)");
            mode = MODE_NORMAL;
            return;
        }
        running = false;
    } else if (strcmp(cmd, "q!") == 0) {
        running = false;
    } else if (strcmp(cmd, "w") == 0) {
        do_save(filename);
        mode = MODE_NORMAL;
    } else if (strncmp(cmd, "w ", 2) == 0) {
        const char *arg = cmd + 2;
        while (*arg == ' ') arg++;
        do_save(arg);
        mode = MODE_NORMAL;
    } else if (strcmp(cmd, "wq") == 0) {
        if (do_save(filename) == 0) running = false;
        else mode = MODE_NORMAL;
    } else if (strncmp(cmd, "wq ", 3) == 0) {
        const char *arg = cmd + 3;
        while (*arg == ' ') arg++;
        if (do_save(arg) == 0) running = false;
        else mode = MODE_NORMAL;
    } else {
        set_status("E: Unknown command");
        mode = MODE_NORMAL;
    }
}

static void handle_command(int key) {
    if (key == KEY_ESCAPE) {
        mode = MODE_NORMAL;
        cmd_buf[0] = '\0';
        cmd_len = 0;
    } else if (key == '\n') {
        cmd_execute();
        cmd_len = 0;
    } else if (key == '\b') {
        if (cmd_len > 1) {
            cmd_len--;
            cmd_buf[cmd_len] = '\0';
        } else {
            mode = MODE_NORMAL;
            cmd_buf[0] = '\0';
            cmd_len = 0;
        }
    } else if (key >= ' ' && key < 127 && cmd_len < 78) {
        cmd_buf[cmd_len++] = (char)key;
        cmd_buf[cmd_len] = '\0';
    }
}

/* ---- Entry point ---- */

void editor_open(const char *fname) {
    serial_log("Editor opening");

    buf_init();
    view_init(&view);
    mode = MODE_NORMAL;
    modified = false;
    cmd_buf[0] = '\0';
    cmd_len = 0;
    running = true;
    awaiting_g = false;
    awaiting_r = false;
    awaiting_gt = false;
    awaiting_lt = false;
    yank_len = 0;
    repeat_count = 0;
    count_active = false;
    pending_operator = OP_NONE;
    op_count = 0;

    if (fname) {
        uint8_t i;
        for (i = 0; fname[i] && i < 12; i++)
            filename[i] = fname[i];
        filename[i] = '\0';

        static uint8_t file_buf[BUFFER_SIZE];
        int bytes = fat12_read_file(fname, file_buf, BUFFER_SIZE);
        if (bytes > 0) {
            buf_load((const char *)file_buf, (uint32_t)bytes);
        }
    } else {
        filename[0] = '\0';
    }

    sync_cursor();

    while (running) {
        const char *cmd_display = (mode == MODE_COMMAND) ? cmd_buf : "";
        if (mode == MODE_NORMAL && cmd_buf[0] && cmd_buf[0] != ':') {
            cmd_display = cmd_buf;
        }
        if (mode == MODE_VISUAL || mode == MODE_VISUAL_LINE) {
            get_sel_bounds(&view.sel_start, &view.sel_end);
        } else {
            view.sel_start = (uint32_t)-1;
            view.sel_end = (uint32_t)-1;
        }

        view_render(&view, filename[0] ? filename : (const char *)0,
                    modified, mode_string(), cmd_display);

        int key = keyboard_getchar();

        if (mode == MODE_NORMAL && cmd_buf[0] && cmd_buf[0] != ':') {
            cmd_buf[0] = '\0';
        }

        switch (mode) {
            case MODE_NORMAL:      handle_normal(key);  break;
            case MODE_INSERT:      handle_insert(key);  break;
            case MODE_COMMAND:     handle_command(key);  break;
            case MODE_VISUAL:
            case MODE_VISUAL_LINE: handle_visual(key);   break;
        }
    }

    vga_clear();
    serial_log("Editor closed");
}
