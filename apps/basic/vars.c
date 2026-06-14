#include "basic/vars.h"
#include "lib/string.h"

static int32_t num_vars[MAX_NUM_VARS];
static char str_vars[MAX_STR_VARS][STR_MAX_LEN + 1];

void vars_clear(void) {
    memset(num_vars, 0, sizeof(num_vars));
    memset(str_vars, 0, sizeof(str_vars));
}

/* Variable index mapping:
   A = 0, B = 1, ..., Z = 25      (index == -1, plain letter)
   A0 = 26, A1 = 27, ..., A9 = 35 (index 0-9)
   B0 = 36, ...
   Z9 = 285
*/
int32_t *var_num(char name, int index) {
    int slot;
    if (name >= 'a' && name <= 'z') name -= 32;
    if (name < 'A' || name > 'Z') return NULL;

    if (index < 0) {
        slot = name - 'A';
    } else if (index <= 9) {
        slot = 26 + (name - 'A') * 10 + index;
    } else {
        return NULL;
    }
    return &num_vars[slot];
}

char *var_str(char name) {
    if (name >= 'a' && name <= 'z') name -= 32;
    if (name < 'A' || name > 'Z') return NULL;
    return str_vars[name - 'A'];
}
