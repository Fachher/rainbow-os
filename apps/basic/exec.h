#ifndef BASIC_EXEC_H
#define BASIC_EXEC_H

#include "include/types.h"

struct exec_state {
    uint8_t *current_line;
    uint8_t  pos;
    bool     running;
    bool     jumped;

    /* GOSUB return stack */
    uint8_t *gosub_line[32];
    uint8_t  gosub_pos[32];
    uint8_t  gosub_sp;

    /* FOR/NEXT stack */
    struct {
        char     var;
        int      var_idx;
        int32_t  limit;
        int32_t  step;
        uint8_t *line;
        uint8_t  pos;
    } for_stack[8];
    uint8_t for_sp;
};

/* Initialize executor state */
void exec_init(struct exec_state *state);

/* Execute one line of tokens */
void exec_line(struct exec_state *state, const uint8_t *tokens, uint8_t len);
void exec_line_at(struct exec_state *state, const uint8_t *tokens, uint8_t len, uint8_t start_pos);

/* Execute in immediate mode (no line number) */
void exec_immediate(const uint8_t *tokens, uint8_t len);

/* Error reporting */
bool        exec_has_error(void);
const char *exec_error_msg(void);
uint16_t    exec_error_line(void);
void        exec_clear_error(void);
void        exec_set_error(const char *msg);

/* Access to exec state for BASIC shell */
extern struct exec_state basic_state;

#endif
