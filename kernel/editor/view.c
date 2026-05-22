#include "view.h"
#include "buffer.h"
#include "drivers/vga.h"
#include "lib/string.h"

void view_init(struct editor_view *v) {
    v->screen_rows = vga_get_rows() - 1;  /* reserve last row for status */
    v->screen_cols = vga_get_cols();
    v->top_line = 0;
    v->cursor_line = 0;
    v->cursor_col = 0;
}

void view_scroll_to_cursor(struct editor_view *v) {
    if (v->cursor_line < v->top_line)
        v->top_line = v->cursor_line;
    if (v->cursor_line >= v->top_line + v->screen_rows)
        v->top_line = v->cursor_line - v->screen_rows + 1;
}

void view_render(struct editor_view *v, const char *filename, bool modified, const char *mode_str, const char *cmd_buf) {
    uint32_t total_lines = buf_line_count();

    /* Draw text area */
    for (uint8_t row = 0; row < v->screen_rows; row++) {
        uint32_t doc_line = v->top_line + row;

        if (doc_line >= total_lines) {
            /* Empty line past EOF: show tilde */
            vga_putchar_at(row, 0, '~', VGA_LIGHT_BLUE, VGA_BLACK);
            for (uint8_t col = 1; col < v->screen_cols; col++)
                vga_putchar_at(row, col, ' ', VGA_WHITE, VGA_BLACK);
        } else {
            uint32_t line_start = buf_line_start(doc_line);
            uint32_t line_len = buf_line_length(doc_line);

            for (uint8_t col = 0; col < v->screen_cols; col++) {
                if (col < line_len) {
                    char c = buf_char_at(line_start + col);
                    if (c == '\t') c = ' ';
                    vga_putchar_at(row, col, c, VGA_WHITE, VGA_BLACK);
                } else {
                    vga_putchar_at(row, col, ' ', VGA_WHITE, VGA_BLACK);
                }
            }
        }
    }

    /* Status bar on last row (inverted colors) */
    uint8_t status_row = v->screen_rows;
    for (uint8_t col = 0; col < v->screen_cols; col++)
        vga_putchar_at(status_row, col, ' ', VGA_BLACK, VGA_WHITE);

    /* Left side: filename + modified flag */
    uint8_t col = 0;
    if (filename) {
        for (uint8_t i = 0; filename[i] && col < 20; i++)
            vga_putchar_at(status_row, col++, filename[i], VGA_BLACK, VGA_WHITE);
    } else {
        const char *noname = "[No Name]";
        for (uint8_t i = 0; noname[i] && col < 20; i++)
            vga_putchar_at(status_row, col++, noname[i], VGA_BLACK, VGA_WHITE);
    }

    if (modified) {
        const char *mod = " [+]";
        for (uint8_t i = 0; mod[i] && col < 30; i++)
            vga_putchar_at(status_row, col++, mod[i], VGA_BLACK, VGA_WHITE);
    }

    /* Center: mode */
    uint8_t mode_col = 35;
    if (mode_str) {
        for (uint8_t i = 0; mode_str[i] && mode_col < 50; i++)
            vga_putchar_at(status_row, mode_col++, mode_str[i], VGA_BLACK, VGA_WHITE);
    }

    /* Right side: line/col */
    char pos_buf[20];
    uint32_t ln = v->cursor_line + 1;
    uint32_t cl = v->cursor_col + 1;
    /* Format "Ln X, Col Y" manually */
    uint8_t pi = 0;
    pos_buf[pi++] = 'L'; pos_buf[pi++] = 'n'; pos_buf[pi++] = ' ';
    /* Line number */
    char num[10]; int ni = 0;
    uint32_t tmp = ln;
    do { num[ni++] = '0' + (tmp % 10); tmp /= 10; } while (tmp > 0);
    while (ni > 0) pos_buf[pi++] = num[--ni];
    pos_buf[pi++] = ','; pos_buf[pi++] = ' ';
    pos_buf[pi++] = 'C'; pos_buf[pi++] = 'o'; pos_buf[pi++] = 'l'; pos_buf[pi++] = ' ';
    tmp = cl; ni = 0;
    do { num[ni++] = '0' + (tmp % 10); tmp /= 10; } while (tmp > 0);
    while (ni > 0) pos_buf[pi++] = num[--ni];
    pos_buf[pi] = '\0';

    uint8_t right_start = v->screen_cols - pi;
    for (uint8_t i = 0; i < pi; i++)
        vga_putchar_at(status_row, right_start + i, pos_buf[i], VGA_BLACK, VGA_WHITE);

    /* If in command mode, show command buffer on status bar */
    if (cmd_buf && cmd_buf[0]) {
        for (uint8_t i = 0; cmd_buf[i] && i < v->screen_cols; i++)
            vga_putchar_at(status_row, i, cmd_buf[i], VGA_BLACK, VGA_WHITE);
    }

    /* Position hardware cursor */
    uint8_t cur_row = (uint8_t)(v->cursor_line - v->top_line);
    uint8_t cur_col = (uint8_t)v->cursor_col;
    if (cur_col >= v->screen_cols) cur_col = v->screen_cols - 1;
    vga_set_cursor(cur_row, cur_col);
}
