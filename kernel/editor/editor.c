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

static struct editor_view view;
static enum editor_mode mode;
static char filename[13];
static bool modified;
static char cmd_buf[80];
static uint8_t cmd_len;
static bool running;
static bool awaiting_g;  /* for gg command */
static bool awaiting_d;  /* for dd command */
static bool awaiting_y;  /* for yy command */
static uint32_t sel_anchor;      /* buffer position where visual selection started */
static char yank_buf[4096];
static uint32_t yank_len;
static bool yank_linewise;       /* true if yanked content is line-wise (dd/yy/V) */

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

/* Sync view cursor from buffer state */
static void sync_cursor(void) {
    view.cursor_line = buf_cursor_line();
    view.cursor_col = buf_cursor_col();
    view_scroll_to_cursor(&view);
}

/* Move cursor to a specific line/col in the buffer */
static void move_to_line_col(uint32_t line, uint32_t col) {
    uint32_t pos = buf_line_start(line);
    uint32_t len = buf_line_length(line);
    if (col > len) col = len;
    buf_move_to(pos + col);
    sync_cursor();
}

/* Move cursor up/down, preserving column where possible */
static void move_vertical(int delta) {
    uint32_t cur_line = buf_cursor_line();
    uint32_t cur_col = buf_cursor_col();
    uint32_t total = buf_line_count();

    int new_line = (int)cur_line + delta;
    if (new_line < 0) new_line = 0;
    if ((uint32_t)new_line >= total) new_line = (int)total - 1;

    move_to_line_col((uint32_t)new_line, cur_col);
}

/* Yank current line into yank_buf */
static void yank_current_line(void) {
    uint32_t line = buf_cursor_line();
    uint32_t start = buf_line_start(line);
    uint32_t len = buf_line_length(line);
    if (len > sizeof(yank_buf) - 1) len = sizeof(yank_buf) - 1;
    for (uint32_t i = 0; i < len; i++)
        yank_buf[i] = buf_char_at(start + i);
    yank_len = len;
}

/* Delete current line (assumes cursor anywhere on line) */
static void delete_current_line(void) {
    uint32_t line = buf_cursor_line();
    uint32_t start = buf_line_start(line);
    uint32_t len = buf_line_length(line);
    buf_move_to(start);
    uint32_t to_delete = len;
    if (start + len < buf_length()) to_delete++;  /* include \n */
    for (uint32_t i = 0; i < to_delete; i++) buf_delete_fwd();
    sync_cursor();
    modified = true;
}

/* Yank a range of buffer positions into yank_buf */
static void yank_range(uint32_t start, uint32_t end) {
    uint32_t len = end - start;
    if (len > sizeof(yank_buf) - 1) len = sizeof(yank_buf) - 1;
    for (uint32_t i = 0; i < len; i++)
        yank_buf[i] = buf_char_at(start + i);
    yank_len = len;
}

/* Delete a range of buffer positions */
static void delete_range(uint32_t start, uint32_t end) {
    buf_move_to(start);
    uint32_t count = end - start;
    for (uint32_t i = 0; i < count; i++)
        buf_delete_fwd();
    sync_cursor();
    modified = true;
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
        /* Expand to full line boundaries */
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
        if (*end < buf_length()) (*end)++;  /* include trailing newline */
    }
}

/* --- Normal Mode --- */

static void handle_normal(int key) {
    if (awaiting_g) {
        awaiting_g = false;
        if (key == 'g') {
            /* gg: go to first line */
            move_to_line_col(0, 0);
        }
        return;
    }
    if (awaiting_d) {
        awaiting_d = false;
        if (key == 'd') {
            yank_current_line();
            yank_linewise = true;
            delete_current_line();
        }
        return;
    }
    if (awaiting_y) {
        awaiting_y = false;
        if (key == 'y') {
            yank_current_line();
            yank_linewise = true;
        }
        return;
    }

    switch (key) {
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
            if (len > 0) end--;  /* on last char, not past it */
            buf_move_to(end);
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
        case 'i':
            mode = MODE_INSERT;
            break;
        case 'a':
            buf_move_right();
            sync_cursor();
            mode = MODE_INSERT;
            break;
        case 'o': {
            /* Open line below */
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
            /* Open line above */
            uint32_t line = buf_cursor_line();
            uint32_t start = buf_line_start(line);
            buf_move_to(start);
            buf_insert('\n');
            buf_move_left();  /* cursor on the new empty line */
            sync_cursor();
            modified = true;
            mode = MODE_INSERT;
            break;
        }
        case 'x': case KEY_DELETE:
            buf_delete_fwd();
            sync_cursor();
            modified = true;
            break;
        case 'd':
            awaiting_d = true;
            break;
        case 'y':
            awaiting_y = true;
            break;
        case 'p': {
            if (yank_len == 0) break;
            if (yank_linewise) {
                /* Paste below current line */
                uint32_t line = buf_cursor_line();
                uint32_t start = buf_line_start(line);
                uint32_t len = buf_line_length(line);
                buf_move_to(start + len);
                buf_insert('\n');
                for (uint32_t i = 0; i < yank_len; i++)
                    buf_insert(yank_buf[i]);
            } else {
                /* Paste after cursor (charwise) */
                buf_move_right();
                for (uint32_t i = 0; i < yank_len; i++)
                    buf_insert(yank_buf[i]);
            }
            sync_cursor();
            modified = true;
            break;
        }
        case 'v':
            sel_anchor = buf_cursor_pos();
            mode = MODE_VISUAL;
            break;
        case 'V':
            sel_anchor = buf_cursor_pos();
            mode = MODE_VISUAL_LINE;
            break;
        case ':':
            mode = MODE_COMMAND;
            cmd_len = 0;
            cmd_buf[0] = ':';
            cmd_buf[1] = '\0';
            cmd_len = 1;
            break;
        case KEY_CTRL('u'): case KEY_PGUP:
            move_vertical(-(int)(view.screen_rows / 2));
            break;
        case KEY_CTRL('d'): case KEY_PGDN:
            move_vertical((int)(view.screen_rows / 2));
            break;
    }
}

/* --- Insert Mode --- */

static void handle_insert(int key) {
    switch (key) {
        case KEY_ESCAPE:
            mode = MODE_NORMAL;
            /* Move cursor back one if not at line start (vim behavior) */
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
            /* Insert 4 spaces */
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

/* --- Visual Mode --- */

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
            uint32_t s, e;
            get_sel_bounds(&s, &e);
            yank_range(s, e);
            yank_linewise = (mode == MODE_VISUAL_LINE);
            delete_range(s, e);
            mode = MODE_NORMAL;
            break;
        }
    }
}

/* --- Command Mode --- */

static void set_status(const char *msg) {
    uint8_t i;
    for (i = 0; msg[i]; i++) cmd_buf[i] = msg[i];
    cmd_buf[i] = '\0';
}

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
    /* If saving with a new name, adopt it */
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

static void cmd_execute(void) {
    /* Skip the leading ':' */
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
            /* Backspace on just ':' exits command mode */
            mode = MODE_NORMAL;
            cmd_buf[0] = '\0';
            cmd_len = 0;
        }
    } else if (key >= ' ' && key < 127 && cmd_len < 78) {
        cmd_buf[cmd_len++] = (char)key;
        cmd_buf[cmd_len] = '\0';
    }
}

/* --- Entry point --- */

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
    awaiting_d = false;
    awaiting_y = false;
    yank_len = 0;

    /* Set filename */
    if (fname) {
        uint8_t i;
        for (i = 0; fname[i] && i < 12; i++)
            filename[i] = fname[i];
        filename[i] = '\0';

        /* Try to load file */
        static uint8_t file_buf[BUFFER_SIZE];
        int bytes = fat12_read_file(fname, file_buf, BUFFER_SIZE);
        if (bytes > 0) {
            buf_load((const char *)file_buf, (uint32_t)bytes);
        }
    } else {
        filename[0] = '\0';
    }

    sync_cursor();

    /* Main loop */
    while (running) {
        const char *cmd_display = (mode == MODE_COMMAND) ? cmd_buf : "";
        /* Check if cmd_buf has an error/info message to show */
        if (mode == MODE_NORMAL && cmd_buf[0] && cmd_buf[0] != ':') {
            cmd_display = cmd_buf;
        }
        /* Compute selection highlight range */
        if (mode == MODE_VISUAL || mode == MODE_VISUAL_LINE) {
            get_sel_bounds(&view.sel_start, &view.sel_end);
        } else {
            view.sel_start = (uint32_t)-1;
            view.sel_end = (uint32_t)-1;
        }

        view_render(&view, filename[0] ? filename : (const char *)0,
                    modified, mode_string(), cmd_display);

        int key = keyboard_getchar();

        /* Clear any status message on next keypress */
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

    /* Restore shell screen */
    vga_clear();
    serial_log("Editor closed");
}
