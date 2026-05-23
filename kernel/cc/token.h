#ifndef CC_TOKEN_H
#define CC_TOKEN_H

enum token_type {
    /* Keywords */
    TOK_INT, TOK_CHAR, TOK_VOID, TOK_IF, TOK_ELSE, TOK_WHILE,
    TOK_FOR, TOK_DO, TOK_RETURN, TOK_BREAK, TOK_CONTINUE,

    /* Literals */
    TOK_NUM_LIT,
    TOK_STR_LIT,
    TOK_CHAR_LIT,

    /* Identifier */
    TOK_IDENT,

    /* Operators */
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_AMP, TOK_PIPE, TOK_CARET, TOK_TILDE, TOK_BANG,
    TOK_AND, TOK_OR,           /* && || */
    TOK_EQ, TOK_NE,           /* == != */
    TOK_LT, TOK_GT, TOK_LE, TOK_GE,
    TOK_SHL, TOK_SHR,         /* << >> */
    TOK_ASSIGN,                /* = */
    TOK_PLUSEQ, TOK_MINUSEQ, TOK_STAREQ, TOK_SLASHEQ,
    TOK_INC, TOK_DEC,         /* ++ -- */

    /* Punctuation */
    TOK_LPAREN, TOK_RPAREN, TOK_LBRACE, TOK_RBRACE,
    TOK_LBRACKET, TOK_RBRACKET,
    TOK_SEMI, TOK_COMMA,

    TOK_EOF
};

#endif
