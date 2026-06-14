#ifndef BASIC_TOKEN_H
#define BASIC_TOKEN_H

/* Statement tokens */
#define TOK_PRINT   0x80
#define TOK_INPUT   0x81
#define TOK_LET     0x82
#define TOK_IF      0x83
#define TOK_THEN    0x84
#define TOK_GOTO    0x85
#define TOK_GOSUB   0x86
#define TOK_RETURN  0x87
#define TOK_FOR     0x88
#define TOK_TO      0x89
#define TOK_STEP    0x8A
#define TOK_NEXT    0x8B
#define TOK_REM     0x8C
#define TOK_END     0x8D
#define TOK_DIM     0x8E
#define TOK_ELSE    0x8F

/* Operator tokens */
#define TOK_AND     0x90
#define TOK_OR      0x91
#define TOK_NOT     0x92
#define TOK_MOD     0x93

/* Comparison tokens */
#define TOK_LE      0x94    /* <= */
#define TOK_GE      0x95    /* >= */
#define TOK_NE      0x96    /* <> */

/* Function tokens */
#define TOK_ABS     0xA0
#define TOK_RND     0xA1
#define TOK_LEN     0xA2
#define TOK_VAL     0xA3
#define TOK_CHR     0xA4    /* CHR$() */
#define TOK_STR     0xA5    /* STR$() */
#define TOK_LEFT    0xA6    /* LEFT$() */
#define TOK_RIGHT   0xA7    /* RIGHT$() */
#define TOK_MID     0xA8    /* MID$() */
#define TOK_PEEK    0xA9
#define TOK_POKE    0xAA
#define TOK_INT     0xAB

/* I/O tokens */
#define TOK_LOAD    0xB0
#define TOK_SAVE    0xB1
#define TOK_LIST    0xB2
#define TOK_RUN     0xB3
#define TOK_NEW     0xB4
#define TOK_CLR     0xB5
#define TOK_CLS     0xB6

/* Special encoding tokens */
#define TOK_NUM     0xFE    /* followed by 4-byte int32 little-endian */
#define TOK_STR_LIT 0xFF   /* followed by length byte + chars */

#endif
