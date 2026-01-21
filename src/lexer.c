#include "ps_lexer.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------- */
/* Helpers                                                   */
/* --------------------------------------------------------- */

static char peek(PSLexer *lx) {
    return lx->src[lx->pos];
}

static char peek_next(PSLexer *lx) {
    if (lx->src[lx->pos] == '\0') return '\0';
    return lx->src[lx->pos + 1];
}

static char peek_n(PSLexer *lx, size_t n) {
    return lx->src[lx->pos + n];
}

static char advance(PSLexer *lx) {
    char c = lx->src[lx->pos++];
    if (c == '\n') {
        lx->line++;
        lx->column = 1;
    } else {
        lx->column++;
    }
    return c;
}

static int match(PSLexer *lx, char c) {
    if (peek(lx) == c) {
        advance(lx);
        return 1;
    }
    return 0;
}

static void skip_whitespace(PSLexer *lx) {
    for (;;) {
        char c = peek(lx);
        if (lx->error) return;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            advance(lx);
            continue;
        }
        if (c == '/' && peek_next(lx) == '/') {
            while (peek(lx) && peek(lx) != '\n') {
                advance(lx);
            }
            continue;
        }
        if (c == '/' && peek_next(lx) == '*') {
            int closed = 0;
            size_t comment_line = lx->line;
            size_t comment_column = lx->column;
            advance(lx);
            advance(lx);
            while (peek(lx)) {
                if (peek(lx) == '*' && peek_next(lx) == '/') {
                    advance(lx);
                    advance(lx);
                    closed = 1;
                    break;
                }
                advance(lx);
            }
            if (!closed) {
                lx->error = 1;
                lx->error_msg = "Parse error: unterminated comment";
                lx->error_line = comment_line;
                lx->error_column = comment_column;
                return;
            }
            continue;
        } else {
            break;
        }
    }
}

static int is_hex_digit(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

static int unicode_escape_after_backslash(PSLexer *lx) {
    return peek(lx) == 'u' &&
           is_hex_digit(peek_n(lx, 1)) &&
           is_hex_digit(peek_n(lx, 2)) &&
           is_hex_digit(peek_n(lx, 3)) &&
           is_hex_digit(peek_n(lx, 4));
}

static void consume_unicode_escape_after_backslash(PSLexer *lx) {
    if (!unicode_escape_after_backslash(lx)) return;
    advance(lx);
    advance(lx);
    advance(lx);
    advance(lx);
    advance(lx);
}

static PSToken make_token(PSTokenType type, const char *start, size_t len, size_t line, size_t column) {
    PSToken t;
    t.type = type;
    t.start = start;
    t.length = len;
    t.number = 0.0;
    t.line = line;
    t.column = column;
    return t;
}

/* --------------------------------------------------------- */
/* Keyword check                                             */
/* --------------------------------------------------------- */

static PSTokenType keyword_type(const char *s, size_t len) {
    if (len == 3 && strncmp(s, "var", 3) == 0) return TOK_VAR;
    if (len == 2 && strncmp(s, "if", 2) == 0) return TOK_IF;
    if (len == 4 && strncmp(s, "else", 4) == 0) return TOK_ELSE;
    if (len == 5 && strncmp(s, "while", 5) == 0) return TOK_WHILE;
    if (len == 2 && strncmp(s, "do", 2) == 0) return TOK_DO;
    if (len == 3 && strncmp(s, "for", 3) == 0) return TOK_FOR;
    if (len == 2 && strncmp(s, "in", 2) == 0) return TOK_IN;
    if (len == 2 && strncmp(s, "of", 2) == 0) return TOK_OF;
    if (len == 6 && strncmp(s, "switch", 6) == 0) return TOK_SWITCH;
    if (len == 4 && strncmp(s, "case", 4) == 0) return TOK_CASE;
    if (len == 7 && strncmp(s, "default", 7) == 0) return TOK_DEFAULT;
    if (len == 8 && strncmp(s, "function", 8) == 0) return TOK_FUNCTION;
    if (len == 6 && strncmp(s, "return", 6) == 0) return TOK_RETURN;
    if (len == 5 && strncmp(s, "break", 5) == 0) return TOK_BREAK;
    if (len == 8 && strncmp(s, "continue", 8) == 0) return TOK_CONTINUE;
    if (len == 4 && strncmp(s, "with", 4) == 0) return TOK_WITH;
    if (len == 3 && strncmp(s, "try", 3) == 0) return TOK_TRY;
    if (len == 5 && strncmp(s, "catch", 5) == 0) return TOK_CATCH;
    if (len == 7 && strncmp(s, "finally", 7) == 0) return TOK_FINALLY;
    if (len == 5 && strncmp(s, "throw", 5) == 0) return TOK_THROW;
    if (len == 3 && strncmp(s, "new", 3) == 0) return TOK_NEW;
    if (len == 4 && strncmp(s, "true", 4) == 0) return TOK_TRUE;
    if (len == 5 && strncmp(s, "false", 5) == 0) return TOK_FALSE;
    if (len == 4 && strncmp(s, "null", 4) == 0) return TOK_NULL;
    if (len == 6 && strncmp(s, "typeof", 6) == 0) return TOK_TYPEOF;
    if (len == 4 && strncmp(s, "void", 4) == 0) return TOK_VOID;
    if (len == 6 && strncmp(s, "delete", 6) == 0) return TOK_DELETE;
    return TOK_IDENTIFIER;
}

/* --------------------------------------------------------- */
/* Public API                                                */
/* --------------------------------------------------------- */

void ps_lexer_init(PSLexer *lx, const char *source) {
    lx->src = source;
    lx->pos = 0;
    lx->line = 1;
    lx->column = 1;
    lx->error = 0;
    lx->error_msg = NULL;
    lx->error_line = 1;
    lx->error_column = 1;
}

PSToken ps_lexer_next(PSLexer *lx) {
    skip_whitespace(lx);
    if (lx->error) {
        return make_token(TOK_EOF, lx->src + lx->pos, 0, lx->line, lx->column);
    }

    const char *start = lx->src + lx->pos;
    size_t line = lx->line;
    size_t column = lx->column;
    char c = advance(lx);

    if (c == '\0') {
        return make_token(TOK_EOF, start, 0, line, column);
    }

    /* Identifiers / keywords */
    if (isalpha((unsigned char)c) || c == '_' || c == '$' || (unsigned char)c >= 128 ||
        (c == '\\' && unicode_escape_after_backslash(lx))) {
        if (c == '\\') {
            consume_unicode_escape_after_backslash(lx);
        }
        for (;;) {
            if (isalnum((unsigned char)peek(lx)) || peek(lx) == '_' || peek(lx) == '$' ||
                (unsigned char)peek(lx) >= 128) {
                advance(lx);
                continue;
            }
            if (peek(lx) == '\\') {
                advance(lx);
                if (unicode_escape_after_backslash(lx)) {
                    consume_unicode_escape_after_backslash(lx);
                    continue;
                }
                break;
            }
            break;
        }
        size_t len = lx->src + lx->pos - start;
        PSTokenType kt = keyword_type(start, len);
        if (kt != TOK_IDENTIFIER && memchr(start, '\\', len)) {
            kt = TOK_IDENTIFIER;
        }
        return make_token(kt, start, len, line, column);
    }

    /* Numbers */
    if (isdigit((unsigned char)c) || (c == '.' && isdigit((unsigned char)peek(lx)))) {
        int saw_dot = 0;
        int saw_exp = 0;
        int has_octal_invalid = 0;
        if (c == '0' && (peek(lx) == 'x' || peek(lx) == 'X')) {
            advance(lx);
            while (is_hex_digit(peek(lx))) advance(lx);
            size_t len = lx->src + lx->pos - start;
            PSToken t = make_token(TOK_NUMBER, start, len, line, column);
            t.number = (double)strtoul(start, NULL, 16);
            return t;
        }
        if (isdigit((unsigned char)c)) {
            while (isdigit((unsigned char)peek(lx))) {
                if (peek(lx) >= '8') has_octal_invalid = 1;
                advance(lx);
            }
        } else {
            saw_dot = 1;
            while (isdigit((unsigned char)peek(lx))) advance(lx);
        }
        if (peek(lx) == '.') {
            saw_dot = 1;
            advance(lx);
            while (isdigit((unsigned char)peek(lx))) advance(lx);
        }
        if (peek(lx) == 'e' || peek(lx) == 'E') {
            saw_exp = 1;
            advance(lx);
            if (peek(lx) == '+' || peek(lx) == '-') advance(lx);
            while (isdigit((unsigned char)peek(lx))) advance(lx);
        }
        size_t len = lx->src + lx->pos - start;
        PSToken t = make_token(TOK_NUMBER, start, len, line, column);
        if (start[0] == '0' && len > 1 && !saw_dot && !saw_exp && !has_octal_invalid) {
            t.number = (double)strtoul(start, NULL, 8);
        } else {
            t.number = strtod(start, NULL);
        }
        return t;
    }

    /* Strings */
    if (c == '"' || c == '\'') {
        char quote = c;
        start = lx->src + lx->pos;
        while (peek(lx)) {
            if (peek(lx) == quote) break;
            if (peek(lx) == '\\') {
                advance(lx);
                if (peek(lx)) advance(lx);
                continue;
            }
            advance(lx);
        }
        size_t len = lx->src + lx->pos - start;
        match(lx, quote);
        return make_token(TOK_STRING, start, len, line, column);
    }

    /* Punctuation */
    switch (c) {
        case '(': return make_token(TOK_LPAREN, start, 1, line, column);
        case ')': return make_token(TOK_RPAREN, start, 1, line, column);
        case '{': return make_token(TOK_LBRACE, start, 1, line, column);
        case '}': return make_token(TOK_RBRACE, start, 1, line, column);
        case '[': return make_token(TOK_LBRACKET, start, 1, line, column);
        case ']': return make_token(TOK_RBRACKET, start, 1, line, column);
        case ';': return make_token(TOK_SEMI, start, 1, line, column);
        case ',': return make_token(TOK_COMMA, start, 1, line, column);
        case '.': return make_token(TOK_DOT, start, 1, line, column);
        case '?': return make_token(TOK_QUESTION, start, 1, line, column);
        case ':': return make_token(TOK_COLON, start, 1, line, column);
        case '=':
            if (match(lx, '=')) {
                if (match(lx, '=')) return make_token(TOK_STRICT_EQ, start, 3, line, column);
                return make_token(TOK_EQ, start, 2, line, column);
            }
            return make_token(TOK_ASSIGN, start, 1, line, column);
        case '+':
            if (match(lx, '+')) return make_token(TOK_PLUS_PLUS, start, 2, line, column);
            if (match(lx, '=')) return make_token(TOK_PLUS_ASSIGN, start, 2, line, column);
            return make_token(TOK_PLUS, start, 1, line, column);
        case '-':
            if (match(lx, '-')) return make_token(TOK_MINUS_MINUS, start, 2, line, column);
            if (match(lx, '=')) return make_token(TOK_MINUS_ASSIGN, start, 2, line, column);
            return make_token(TOK_MINUS, start, 1, line, column);
        case '*':
            if (match(lx, '=')) return make_token(TOK_STAR_ASSIGN, start, 2, line, column);
            return make_token(TOK_STAR, start, 1, line, column);
        case '/':
            if (match(lx, '=')) return make_token(TOK_SLASH_ASSIGN, start, 2, line, column);
            return make_token(TOK_SLASH, start, 1, line, column);
        case '%':
            if (match(lx, '=')) return make_token(TOK_PERCENT_ASSIGN, start, 2, line, column);
            return make_token(TOK_PERCENT, start, 1, line, column);
        case '<':
            if (match(lx, '<')) {
                if (match(lx, '=')) return make_token(TOK_SHL_ASSIGN, start, 3, line, column);
                return make_token(TOK_SHL, start, 2, line, column);
            }
            if (match(lx, '=')) return make_token(TOK_LTE, start, 2, line, column);
            return make_token(TOK_LT, start, 1, line, column);
        case '>':
            if (match(lx, '>')) {
                if (match(lx, '>')) {
                    if (match(lx, '=')) return make_token(TOK_USHR_ASSIGN, start, 4, line, column);
                    return make_token(TOK_USHR, start, 3, line, column);
                }
                if (match(lx, '=')) return make_token(TOK_SHR_ASSIGN, start, 3, line, column);
                return make_token(TOK_SHR, start, 2, line, column);
            }
            if (match(lx, '=')) return make_token(TOK_GTE, start, 2, line, column);
            return make_token(TOK_GT, start, 1, line, column);
        case '!':
            if (match(lx, '=')) {
                if (match(lx, '=')) return make_token(TOK_STRICT_NEQ, start, 3, line, column);
                return make_token(TOK_NEQ, start, 2, line, column);
            }
            return make_token(TOK_NOT, start, 1, line, column);
        case '~': return make_token(TOK_BIT_NOT, start, 1, line, column);
        case '&':
            if (match(lx, '&')) return make_token(TOK_AND_AND, start, 2, line, column);
            if (match(lx, '=')) return make_token(TOK_AND_ASSIGN, start, 2, line, column);
            return make_token(TOK_AND, start, 1, line, column);
        case '|':
            if (match(lx, '|')) return make_token(TOK_OR_OR, start, 2, line, column);
            if (match(lx, '=')) return make_token(TOK_OR_ASSIGN, start, 2, line, column);
            return make_token(TOK_OR, start, 1, line, column);
        case '^':
            if (match(lx, '=')) return make_token(TOK_XOR_ASSIGN, start, 2, line, column);
            return make_token(TOK_XOR, start, 1, line, column);
    }

    /* Unknown character → skip */
    return make_token(TOK_EOF, start, 1, line, column);
}
