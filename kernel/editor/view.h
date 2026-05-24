#ifndef VIEW_H
#define VIEW_H

#include "include/types.h"

struct editor_view {
    uint8_t  screen_rows;    /* usable rows (24 = 25 minus status bar) */
    uint8_t  screen_cols;    /* 80 */
    uint32_t top_line;       /* first visible line */
    uint32_t cursor_line;    /* cursor line in document */
    uint32_t cursor_col;     /* cursor column in document */
    uint32_t sel_start;      /* selection start (buf pos), UINT32_MAX if none */
    uint32_t sel_end;        /* selection end (exclusive) */
};

void view_init(struct editor_view *v);
void view_render(struct editor_view *v, const char *filename, bool modified, const char *mode_str, const char *cmd_buf);
void view_scroll_to_cursor(struct editor_view *v);

#endif
