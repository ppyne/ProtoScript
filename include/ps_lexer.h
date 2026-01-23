#ifndef PS_LEXER_H
#define PS_LEXER_H

#include <stddef.h>

typedef enum {
    TOK_EOF,

    /* literals */
    TOK_IDENTIFIER,
    TOK_NUMBER,
    TOK_STRING,

    /* keywords */
    TOK_VAR,
    TOK_IF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_DO,
    TOK_FOR,
    TOK_IN,
    TOK_OF,
    TOK_SWITCH,
    TOK_CASE,
    TOK_DEFAULT,
    TOK_FUNCTION,
    TOK_RETURN,
    TOK_BREAK,
    TOK_CONTINUE,
    TOK_WITH,
    TOK_TRY,
    TOK_CATCH,
    TOK_FINALLY,
    TOK_THROW,
    TOK_NEW,
    TOK_INSTANCEOF,
    TOK_TRUE,
    TOK_FALSE,
    TOK_NULL,
    TOK_THIS,
    TOK_TYPEOF,
    TOK_VOID,
    TOK_DELETE,
    TOK_INCLUDE,

    /* operators / punctuation */
    TOK_LPAREN, TOK_RPAREN,
    TOK_LBRACE, TOK_RBRACE,
    TOK_LBRACKET, TOK_RBRACKET,
    TOK_SEMI,
    TOK_COMMA,
    TOK_DOT,
    TOK_QUESTION,
    TOK_COLON,
    TOK_ASSIGN,       /* = */
    TOK_PLUS,         /* + */
    TOK_MINUS,        /* - */
    TOK_STAR,         /* * */
    TOK_SLASH,        /* / */
    TOK_PERCENT,      /* % */
    TOK_PLUS_PLUS,    /* ++ */
    TOK_MINUS_MINUS,  /* -- */
    TOK_PLUS_ASSIGN,  /* += */
    TOK_MINUS_ASSIGN, /* -= */
    TOK_STAR_ASSIGN,  /* *= */
    TOK_SLASH_ASSIGN, /* /= */
    TOK_PERCENT_ASSIGN, /* %= */
    TOK_LT,           /* < */
    TOK_LTE,          /* <= */
    TOK_GT,           /* > */
    TOK_GTE,          /* >= */
    TOK_EQ,           /* == */
    TOK_NEQ,          /* != */
    TOK_STRICT_EQ,    /* === */
    TOK_STRICT_NEQ,   /* !== */
    TOK_NOT,          /* ! */
    TOK_BIT_NOT,      /* ~ */
    TOK_AND,          /* & */
    TOK_OR,           /* | */
    TOK_XOR,          /* ^ */
    TOK_AND_AND,      /* && */
    TOK_OR_OR,        /* || */
    TOK_SHL,          /* << */
    TOK_SHR,          /* >> */
    TOK_USHR,         /* >>> */
    TOK_AND_ASSIGN,   /* &= */
    TOK_OR_ASSIGN,    /* |= */
    TOK_XOR_ASSIGN,   /* ^= */
    TOK_SHL_ASSIGN,   /* <<= */
    TOK_SHR_ASSIGN,   /* >>= */
    TOK_USHR_ASSIGN,  /* >>>= */
} PSTokenType;

typedef struct {
    PSTokenType type;
    const char *start;   /* pointer into source */
    size_t      length;  /* byte length */
    double      number;  /* for TOK_NUMBER */
    size_t      line;    /* 1-based */
    size_t      column;  /* 1-based */
} PSToken;

typedef struct {
    const char *src;
    size_t      pos;
    size_t      line;
    size_t      column;
    int         error;
    const char *error_msg;
    size_t      error_line;
    size_t      error_column;
} PSLexer;

/* API */
void     ps_lexer_init(PSLexer *lx, const char *source);
PSToken  ps_lexer_next(PSLexer *lx);

#endif
