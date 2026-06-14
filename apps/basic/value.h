#ifndef BASIC_VALUE_H
#define BASIC_VALUE_H

#include "include/types.h"

enum val_type { VAL_NUM, VAL_STR };

struct bas_value {
    enum val_type type;
    union {
        int32_t num;
        struct {
            char   *ptr;
            uint8_t len;
        } str;
    };
};

#endif
