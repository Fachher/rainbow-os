#ifndef BASIC_VARS_H
#define BASIC_VARS_H

#include "include/types.h"

#define MAX_NUM_VARS 286
#define MAX_STR_VARS 26
#define STR_MAX_LEN  80

void     vars_clear(void);
int32_t *var_num(char name, int index);    /* index -1 for plain A, 0-9 for A0-A9 */
char    *var_str(char name);               /* A$ .. Z$ */

#endif
