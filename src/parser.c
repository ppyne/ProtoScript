#include "ps_lexer.h"
#include "ps_ast.h"
#include "ps_string.h"
#include "ps_config.h"

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>

/* --------------------------------------------------------- */
/* Parser structure                                          */
/* --------------------------------------------------------- */

typedef struct {
    PSLexer  lexer;
    PSToken current;
} PSParser;

static jmp_buf *g_parse_jmp = NULL;

static void parse_error(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    if (g_parse_jmp) {
        longjmp(*g_parse_jmp, 1);
    }
    exit(1);
}

/* --------------------------------------------------------- */
/* Helpers                                                   */
/* --------------------------------------------------------- */

static void advance(PSParser *p) {
    p->current = ps_lexer_next(&p->lexer);
    if (p->lexer.error) {
        parse_error(p->lexer.error_msg ? p->lexer.error_msg : "Parse error");
    }
}

static int match(PSParser *p, PSTokenType type) {
    if (p->current.type == type) {
        advance(p);
        return 1;
    }
    return 0;
}

static void expect(PSParser *p, PSTokenType type, const char *msg) {
    if (!match(p, type)) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Parse error: expected %s", msg);
        parse_error(buf);
    }
}

static int hex_value(unsigned char c) {
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'a' && c <= 'f') return (int)(c - 'a') + 10;
    if (c >= 'A' && c <= 'F') return (int)(c - 'A') + 10;
    return -1;
}

static size_t append_utf8(char *buf, size_t pos, uint32_t codepoint) {
    if (codepoint <= 0x7F) {
        buf[pos++] = (char)codepoint;
    } else if (codepoint <= 0x7FF) {
        buf[pos++] = (char)(0xC0 | ((codepoint >> 6) & 0x1F));
        buf[pos++] = (char)(0x80 | (codepoint & 0x3F));
    } else if (codepoint <= 0xFFFF) {
        buf[pos++] = (char)(0xE0 | ((codepoint >> 12) & 0x0F));
        buf[pos++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        buf[pos++] = (char)(0x80 | (codepoint & 0x3F));
    } else if (codepoint <= 0x10FFFF) {
        buf[pos++] = (char)(0xF0 | ((codepoint >> 18) & 0x07));
        buf[pos++] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        buf[pos++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        buf[pos++] = (char)(0x80 | (codepoint & 0x3F));
    }
    return pos;
}

static char *decode_identifier(const char *start, size_t len, size_t *out_len) {
    size_t cap = len * 4 + 1;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    size_t out = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)start[i];
        if (c != '\\') {
            buf[out++] = (char)c;
            continue;
        }
        if (i + 5 >= len || start[i + 1] != 'u') {
            free(buf);
            parse_error("Parse error: invalid identifier escape");
        }
        int h1 = hex_value((unsigned char)start[i + 2]);
        int h2 = hex_value((unsigned char)start[i + 3]);
        int h3 = hex_value((unsigned char)start[i + 4]);
        int h4 = hex_value((unsigned char)start[i + 5]);
        if (h1 < 0 || h2 < 0 || h3 < 0 || h4 < 0) {
            free(buf);
            parse_error("Parse error: invalid identifier escape");
        }
        uint32_t value = (uint32_t)((h1 << 12) | (h2 << 8) | (h3 << 4) | h4);
        out = append_utf8(buf, out, value);
        i += 5;
    }
    buf[out] = '\0';
    if (out_len) *out_len = out;
    return buf;
}

static PSAstNode *parse_identifier_token(PSToken tok) {
    if (memchr(tok.start, '\\', tok.length)) {
        size_t out_len = 0;
        char *decoded = decode_identifier(tok.start, tok.length, &out_len);
        if (!decoded) {
            parse_error("Parse error: out of memory");
        }
        PSAstNode *node = ps_ast_identifier(decoded, out_len);
        free(decoded);
        return node;
    }
    return ps_ast_identifier(tok.start, tok.length);
}
static PSString *parse_string_literal(const char *start, size_t len) {
    size_t cap = len * 4 + 1;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        return ps_string_from_cstr("");
    }
    size_t out = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)start[i];
        if (c != '\\') {
            buf[out++] = (char)c;
            continue;
        }
        if (i + 1 >= len) {
            buf[out++] = '\\';
            break;
        }
        unsigned char esc = (unsigned char)start[++i];
        if (esc >= '0' && esc <= '7') {
            int value = esc - '0';
            int digits = 1;
            while (digits < 3 && i + 1 < len) {
                unsigned char next = (unsigned char)start[i + 1];
                if (next < '0' || next > '7') break;
                i++;
                value = (value * 8) + (int)(next - '0');
                digits++;
            }
            out = append_utf8(buf, out, (uint32_t)value);
            continue;
        }
        switch (esc) {
            case 'n': buf[out++] = '\n'; break;
            case 'r': buf[out++] = '\r'; break;
            case 't': buf[out++] = '\t'; break;
            case 'b': buf[out++] = '\b'; break;
            case 'f': buf[out++] = '\f'; break;
            case 'v': buf[out++] = '\v'; break;
            case '"': buf[out++] = '"'; break;
            case '\\': buf[out++] = '\\'; break;
            case 'x': {
                if (i + 2 < len) {
                    int hi = hex_value((unsigned char)start[i + 1]);
                    int lo = hex_value((unsigned char)start[i + 2]);
                    if (hi >= 0 && lo >= 0) {
                        uint32_t value = (uint32_t)((hi << 4) | lo);
                        out = append_utf8(buf, out, value);
                        i += 2;
                        break;
                    }
                }
                buf[out++] = 'x';
                break;
            }
            case 'u': {
                if (i + 4 < len) {
                    int h1 = hex_value((unsigned char)start[i + 1]);
                    int h2 = hex_value((unsigned char)start[i + 2]);
                    int h3 = hex_value((unsigned char)start[i + 3]);
                    int h4 = hex_value((unsigned char)start[i + 4]);
                    if (h1 >= 0 && h2 >= 0 && h3 >= 0 && h4 >= 0) {
                        uint32_t value = (uint32_t)((h1 << 12) | (h2 << 8) | (h3 << 4) | h4);
                        out = append_utf8(buf, out, value);
                        i += 4;
                        break;
                    }
                }
                buf[out++] = 'u';
                break;
            }
            default:
                buf[out++] = (char)esc;
                break;
        }
    }
    PSString *s = ps_string_from_utf8(buf, out);
    free(buf);
    return s;
}

/* --------------------------------------------------------- */
/* Forward declarations                                      */
/* --------------------------------------------------------- */

static PSAstNode *parse_statement(PSParser *p);
static PSAstNode *parse_expression(PSParser *p);
static PSAstNode *parse_var_decl_list(PSParser *p);
static PSAstNode *parse_comma(PSParser *p);
static PSAstNode *parse_assignment(PSParser *p);
static PSAstNode *parse_conditional(PSParser *p);
static PSAstNode *parse_logical_or(PSParser *p);
static PSAstNode *parse_logical_and(PSParser *p);
static PSAstNode *parse_bitwise_or(PSParser *p);
static PSAstNode *parse_bitwise_xor(PSParser *p);
static PSAstNode *parse_bitwise_and(PSParser *p);
static PSAstNode *parse_equality(PSParser *p);
static PSAstNode *parse_relational(PSParser *p);
static PSAstNode *parse_shift(PSParser *p);
static PSAstNode *parse_additive(PSParser *p);
static PSAstNode *parse_multiplicative(PSParser *p);
static PSAstNode *parse_unary(PSParser *p);
static PSAstNode *parse_postfix(PSParser *p);
static PSAstNode *parse_primary(PSParser *p);
static PSAstNode *parse_primary_atom(PSParser *p);
static PSAstNode *parse_regex_literal(PSParser *p);
static PSAstNode *parse_member_base(PSParser *p);
static PSAstNode *parse_member(PSParser *p);
static PSAstNode *parse_block(PSParser *p);
static PSAstNode *parse_switch(PSParser *p);

/* --------------------------------------------------------- */
/* Entry point                                               */
/* --------------------------------------------------------- */

PSAstNode *ps_parse(const char *source) {
    PSParser p;
    ps_lexer_init(&p.lexer, source);
    jmp_buf jmp_env;
    g_parse_jmp = &jmp_env;
    if (setjmp(jmp_env) != 0) {
        g_parse_jmp = NULL;
        return NULL;
    }

    advance(&p);

    PSAstNode **items = NULL;
    size_t count = 0;

    while (p.current.type != TOK_EOF) {
        items = realloc(items, sizeof(PSAstNode *) * (count + 1));
        items[count++] = parse_statement(&p);
    }

    g_parse_jmp = NULL;
    return ps_ast_program(items, count);
}

/* --------------------------------------------------------- */
/* Statements                                                */
/* --------------------------------------------------------- */

static PSAstNode *parse_var_decl_list(PSParser *p) {
    PSAstNode **decls = NULL;
    size_t count = 0;

    do {
        PSToken id = p->current;
        expect(p, TOK_IDENTIFIER, "identifier");
        PSAstNode *id_node = parse_identifier_token(id);

        PSAstNode *init = NULL;
        if (match(p, TOK_ASSIGN)) {
            init = parse_expression(p);
        }

        PSAstNode *decl = ps_ast_var_decl(id_node, init);
        decls = realloc(decls, sizeof(PSAstNode *) * (count + 1));
        decls[count++] = decl;
    } while (match(p, TOK_COMMA));

    if (count == 1) {
        PSAstNode *only = decls[0];
        free(decls);
        return only;
    }

    return ps_ast_block(decls, count);
}

static PSAstNode *parse_statement(PSParser *p) {
    /* label */
    if (p->current.type == TOK_IDENTIFIER) {
        PSParser saved = *p;
        PSToken label = p->current;
        advance(p);
        if (match(p, TOK_COLON)) {
            PSAstNode *label_node = parse_identifier_token(label);
            PSAstNode *stmt = parse_statement(p);
            switch (stmt->kind) {
                case AST_WHILE:
                    stmt->as.while_stmt.label = label_node;
                    return stmt;
                case AST_DO_WHILE:
                    stmt->as.do_while.label = label_node;
                    return stmt;
                case AST_FOR:
                    stmt->as.for_stmt.label = label_node;
                    return stmt;
                case AST_FOR_IN:
                    stmt->as.for_in.label = label_node;
                    return stmt;
                case AST_SWITCH:
                    stmt->as.switch_stmt.label = label_node;
                    return stmt;
                default:
                    return ps_ast_label(label_node, stmt);
            }
        }
        *p = saved;
    }

    /* block */
    if (match(p, TOK_LBRACE)) {
        return parse_block(p);
    }

    /* var declaration */
    if (match(p, TOK_VAR)) {
        PSAstNode *decls = parse_var_decl_list(p);
        expect(p, TOK_SEMI, "';'");
        return decls;
    }

    /* function declaration */
    if (match(p, TOK_FUNCTION)) {
        PSToken id = p->current;
        expect(p, TOK_IDENTIFIER, "function name");
        PSAstNode *id_node = parse_identifier_token(id);

        expect(p, TOK_LPAREN, "'('");
        PSAstNode **params = NULL;
        PSAstNode **param_defaults = NULL;
        size_t param_count = 0;
        if (p->current.type != TOK_RPAREN) {
            do {
                PSToken param = p->current;
                expect(p, TOK_IDENTIFIER, "parameter name");
                params = realloc(params, sizeof(PSAstNode *) * (param_count + 1));
                param_defaults = realloc(param_defaults, sizeof(PSAstNode *) * (param_count + 1));
                params[param_count] = parse_identifier_token(param);
                param_defaults[param_count] = NULL;
                if (match(p, TOK_ASSIGN)) {
                    param_defaults[param_count] = parse_assignment(p);
                }
                param_count++;
            } while (match(p, TOK_COMMA));
        }
        expect(p, TOK_RPAREN, "')'");

        expect(p, TOK_LBRACE, "'{'");
        PSAstNode *body = parse_block(p);
        return ps_ast_func_decl(id_node, params, param_defaults, param_count, body);
    }

    /* if statement */
    if (match(p, TOK_IF)) {
        expect(p, TOK_LPAREN, "'('");
        PSAstNode *cond = parse_expression(p);
        expect(p, TOK_RPAREN, "')'");
        PSAstNode *then_branch = parse_statement(p);
        PSAstNode *else_branch = NULL;
        if (match(p, TOK_ELSE)) {
            else_branch = parse_statement(p);
        }
        return ps_ast_if(cond, then_branch, else_branch);
    }

    /* while statement */
    if (match(p, TOK_WHILE)) {
        expect(p, TOK_LPAREN, "'('");
        PSAstNode *cond = parse_expression(p);
        expect(p, TOK_RPAREN, "')'");
        PSAstNode *body = parse_statement(p);
        return ps_ast_while(cond, body);
    }

    /* do/while statement */
    if (match(p, TOK_DO)) {
        PSAstNode *body = parse_statement(p);
        expect(p, TOK_WHILE, "'while'");
        expect(p, TOK_LPAREN, "'('");
        PSAstNode *cond = parse_expression(p);
        expect(p, TOK_RPAREN, "')'");
        expect(p, TOK_SEMI, "';'");
        return ps_ast_do_while(body, cond);
    }

    /* for statement */
    if (match(p, TOK_FOR)) {
        expect(p, TOK_LPAREN, "'('");
        PSParser saved = *p;

        if (match(p, TOK_VAR)) {
            PSToken id = p->current;
            expect(p, TOK_IDENTIFIER, "identifier");
            PSAstNode *id_node = parse_identifier_token(id);
            if (match(p, TOK_IN)) {
                PSAstNode *obj = parse_expression(p);
                expect(p, TOK_RPAREN, "')'");
                PSAstNode *body = parse_statement(p);
                return ps_ast_for_in(id_node, obj, body, 1);
            }
        } else {
            PSAstNode *left = parse_member(p);
            if (match(p, TOK_IN)) {
                PSAstNode *obj = parse_expression(p);
                expect(p, TOK_RPAREN, "')'");
                PSAstNode *body = parse_statement(p);
                return ps_ast_for_in(left, obj, body, 0);
            }
        }

        *p = saved;

        PSAstNode *init = NULL;
        if (match(p, TOK_SEMI)) {
            init = NULL;
        } else if (match(p, TOK_VAR)) {
            PSAstNode *decls = parse_var_decl_list(p);
            expect(p, TOK_SEMI, "';'");
            init = decls;
        } else {
            PSAstNode *expr = parse_expression(p);
            expect(p, TOK_SEMI, "';'");
            init = ps_ast_expr_stmt(expr);
        }

        PSAstNode *test = NULL;
        if (p->current.type != TOK_SEMI) {
            test = parse_expression(p);
        }
        expect(p, TOK_SEMI, "';'");

        PSAstNode *update = NULL;
        if (p->current.type != TOK_RPAREN) {
            update = parse_expression(p);
        }
        expect(p, TOK_RPAREN, "')'");

        PSAstNode *body = parse_statement(p);
        return ps_ast_for(init, test, update, body);
    }

    /* switch statement */
    if (match(p, TOK_SWITCH)) {
        return parse_switch(p);
    }

    /* break statement */
    if (match(p, TOK_BREAK)) {
        PSAstNode *label = NULL;
        if (p->current.type == TOK_IDENTIFIER) {
            PSToken id = p->current;
            advance(p);
            label = parse_identifier_token(id);
        }
        expect(p, TOK_SEMI, "';'");
        return ps_ast_break(label);
    }

    /* continue statement */
    if (match(p, TOK_CONTINUE)) {
        PSAstNode *label = NULL;
        if (p->current.type == TOK_IDENTIFIER) {
            PSToken id = p->current;
            advance(p);
            label = parse_identifier_token(id);
        }
        expect(p, TOK_SEMI, "';'");
        return ps_ast_continue(label);
    }

    /* with statement */
    if (match(p, TOK_WITH)) {
#if !PS_ENABLE_WITH
        parse_error("Parse error: 'with' is disabled");
#endif
        expect(p, TOK_LPAREN, "'('");
        PSAstNode *obj = parse_expression(p);
        expect(p, TOK_RPAREN, "')'");
        PSAstNode *body = parse_statement(p);
        return ps_ast_with(obj, body);
    }

    /* return statement */
    if (match(p, TOK_RETURN)) {
        PSAstNode *expr = NULL;
        if (p->current.type != TOK_SEMI) {
            expr = parse_expression(p);
        }
        expect(p, TOK_SEMI, "';'");
        return ps_ast_return(expr);
    }

    /* throw statement */
    if (match(p, TOK_THROW)) {
        PSAstNode *expr = parse_expression(p);
        expect(p, TOK_SEMI, "';'");
        return ps_ast_throw(expr);
    }

    /* try/catch/finally */
    if (match(p, TOK_TRY)) {
        expect(p, TOK_LBRACE, "'{'");
        PSAstNode *try_block = parse_block(p);

        PSAstNode *catch_param = NULL;
        PSAstNode *catch_block = NULL;
        PSAstNode *finally_block = NULL;

        if (match(p, TOK_CATCH)) {
            expect(p, TOK_LPAREN, "'('");
            PSToken id = p->current;
            expect(p, TOK_IDENTIFIER, "identifier");
            catch_param = parse_identifier_token(id);
            expect(p, TOK_RPAREN, "')'");
            expect(p, TOK_LBRACE, "'{'");
            catch_block = parse_block(p);
        }

        if (match(p, TOK_FINALLY)) {
            expect(p, TOK_LBRACE, "'{'");
            finally_block = parse_block(p);
        }

        if (!catch_block && !finally_block) {
            parse_error("Parse error: try must have catch or finally");
        }

        return ps_ast_try(try_block, catch_param, catch_block, finally_block);
    }

    /* expression statement */
    PSAstNode *expr = parse_expression(p);
    expect(p, TOK_SEMI, "';'");
    return ps_ast_expr_stmt(expr);
}

/* --------------------------------------------------------- */
/* Expressions                                               */
/* --------------------------------------------------------- */

static PSAstNode *parse_expression(PSParser *p) {
    return parse_comma(p);
}

static PSAstNode *parse_comma(PSParser *p) {
    PSAstNode *left = parse_assignment(p);
    while (match(p, TOK_COMMA)) {
        PSAstNode *right = parse_assignment(p);
        left = ps_ast_binary(TOK_COMMA, left, right);
    }
    return left;
}

static PSAstNode *parse_assignment(PSParser *p) {
    PSAstNode *left = parse_conditional(p);

    if (p->current.type == TOK_ASSIGN ||
        p->current.type == TOK_PLUS_ASSIGN ||
        p->current.type == TOK_MINUS_ASSIGN ||
        p->current.type == TOK_STAR_ASSIGN ||
        p->current.type == TOK_SLASH_ASSIGN ||
        p->current.type == TOK_PERCENT_ASSIGN ||
        p->current.type == TOK_AND_ASSIGN ||
        p->current.type == TOK_OR_ASSIGN ||
        p->current.type == TOK_XOR_ASSIGN ||
        p->current.type == TOK_SHL_ASSIGN ||
        p->current.type == TOK_SHR_ASSIGN ||
        p->current.type == TOK_USHR_ASSIGN) {
        int op = p->current.type;
        advance(p);
        if (left->kind != AST_IDENTIFIER && left->kind != AST_MEMBER) {
            parse_error("Parse error: invalid assignment target");
        }
        PSAstNode *value = parse_assignment(p);
        return ps_ast_assign(op, left, value);
    }

    return left;
}

static PSAstNode *parse_conditional(PSParser *p) {
    PSAstNode *cond = parse_logical_or(p);
    if (match(p, TOK_QUESTION)) {
        PSAstNode *then_expr = parse_assignment(p);
        expect(p, TOK_COLON, "':'");
        PSAstNode *else_expr = parse_assignment(p);
        return ps_ast_conditional(cond, then_expr, else_expr);
    }
    return cond;
}

static PSAstNode *parse_logical_or(PSParser *p) {
    PSAstNode *left = parse_logical_and(p);
    while (p->current.type == TOK_OR_OR) {
        int op = p->current.type;
        advance(p);
        PSAstNode *right = parse_logical_and(p);
        left = ps_ast_binary(op, left, right);
    }
    return left;
}

static PSAstNode *parse_logical_and(PSParser *p) {
    PSAstNode *left = parse_bitwise_or(p);
    while (p->current.type == TOK_AND_AND) {
        int op = p->current.type;
        advance(p);
        PSAstNode *right = parse_bitwise_or(p);
        left = ps_ast_binary(op, left, right);
    }
    return left;
}

static PSAstNode *parse_bitwise_or(PSParser *p) {
    PSAstNode *left = parse_bitwise_xor(p);
    while (p->current.type == TOK_OR) {
        int op = p->current.type;
        advance(p);
        PSAstNode *right = parse_bitwise_xor(p);
        left = ps_ast_binary(op, left, right);
    }
    return left;
}

static PSAstNode *parse_bitwise_xor(PSParser *p) {
    PSAstNode *left = parse_bitwise_and(p);
    while (p->current.type == TOK_XOR) {
        int op = p->current.type;
        advance(p);
        PSAstNode *right = parse_bitwise_and(p);
        left = ps_ast_binary(op, left, right);
    }
    return left;
}

static PSAstNode *parse_bitwise_and(PSParser *p) {
    PSAstNode *left = parse_equality(p);
    while (p->current.type == TOK_AND) {
        int op = p->current.type;
        advance(p);
        PSAstNode *right = parse_equality(p);
        left = ps_ast_binary(op, left, right);
    }
    return left;
}

static PSAstNode *parse_equality(PSParser *p) {
    PSAstNode *left = parse_relational(p);
    while (p->current.type == TOK_EQ ||
           p->current.type == TOK_NEQ ||
           p->current.type == TOK_STRICT_EQ ||
           p->current.type == TOK_STRICT_NEQ) {
        int op = p->current.type;
        advance(p);
        PSAstNode *right = parse_relational(p);
        left = ps_ast_binary(op, left, right);
    }
    return left;
}

static PSAstNode *parse_relational(PSParser *p) {
    PSAstNode *left = parse_shift(p);

    while (p->current.type == TOK_LT ||
           p->current.type == TOK_LTE ||
           p->current.type == TOK_GT ||
           p->current.type == TOK_GTE) {
        int op = p->current.type;
        advance(p);
        PSAstNode *right = parse_shift(p);
        left = ps_ast_binary(op, left, right);
    }

    return left;
}

static PSAstNode *parse_shift(PSParser *p) {
    PSAstNode *left = parse_additive(p);
    while (p->current.type == TOK_SHL ||
           p->current.type == TOK_SHR ||
           p->current.type == TOK_USHR) {
        int op = p->current.type;
        advance(p);
        PSAstNode *right = parse_additive(p);
        left = ps_ast_binary(op, left, right);
    }
    return left;
}

static PSAstNode *parse_additive(PSParser *p) {
    PSAstNode *left = parse_multiplicative(p);

    while (p->current.type == TOK_PLUS || p->current.type == TOK_MINUS) {
        int op = p->current.type;
        advance(p);
        PSAstNode *right = parse_multiplicative(p);
        left = ps_ast_binary(op, left, right);
    }

    return left;
}

static PSAstNode *parse_multiplicative(PSParser *p) {
    PSAstNode *left = parse_unary(p);
    while (p->current.type == TOK_STAR ||
           p->current.type == TOK_SLASH ||
           p->current.type == TOK_PERCENT) {
        int op = p->current.type;
        advance(p);
        PSAstNode *right = parse_unary(p);
        left = ps_ast_binary(op, left, right);
    }
    return left;
}

static PSAstNode *parse_unary(PSParser *p) {
    if (p->current.type == TOK_NOT ||
        p->current.type == TOK_BIT_NOT ||
        p->current.type == TOK_PLUS ||
        p->current.type == TOK_MINUS ||
        p->current.type == TOK_TYPEOF ||
        p->current.type == TOK_VOID ||
        p->current.type == TOK_DELETE) {
        int op = p->current.type;
        advance(p);
        PSAstNode *expr = parse_unary(p);
        return ps_ast_unary(op, expr);
    }

    if (p->current.type == TOK_PLUS_PLUS ||
        p->current.type == TOK_MINUS_MINUS) {
        int op = p->current.type;
        advance(p);
        PSAstNode *expr = parse_unary(p);
        return ps_ast_update(op, 1, expr);
    }

    return parse_postfix(p);
}

static PSAstNode *parse_postfix(PSParser *p) {
    PSAstNode *expr = parse_member(p);
    if (p->current.type == TOK_PLUS_PLUS ||
        p->current.type == TOK_MINUS_MINUS) {
        int op = p->current.type;
        advance(p);
        return ps_ast_update(op, 0, expr);
    }
    return expr;
}

/* --------------------------------------------------------- */
/* Member / Call                                             */
/* --------------------------------------------------------- */

static PSAstNode *parse_member(PSParser *p) {
    PSAstNode *expr = parse_primary(p);

    for (;;) {
        /* obj.prop */
        if (match(p, TOK_DOT)) {
            PSToken id = p->current;
            expect(p, TOK_IDENTIFIER, "property identifier");
            PSAstNode *prop =
                parse_identifier_token(id);
            expr = ps_ast_member(expr, prop, 0);
            continue;
        }

        /* obj[expr] */
        if (match(p, TOK_LBRACKET)) {
            PSAstNode *prop = parse_expression(p);
            expect(p, TOK_RBRACKET, "']'");
            expr = ps_ast_member(expr, prop, 1);
            continue;
        }

        /* call */
        if (match(p, TOK_LPAREN)) {
            PSAstNode **args = NULL;
            size_t argc = 0;

            if (p->current.type != TOK_RPAREN) {
                do {
                    args = realloc(args, sizeof(PSAstNode *) * (argc + 1));
                    args[argc++] = parse_assignment(p);
                } while (match(p, TOK_COMMA));
            }

            expect(p, TOK_RPAREN, "')'");
            expr = ps_ast_call(expr, args, argc);
            continue;
        }

        break;
    }

    return expr;
}

/* --------------------------------------------------------- */
/* Primary                                                   */
/* --------------------------------------------------------- */

static PSAstNode *parse_primary(PSParser *p) {
    if (match(p, TOK_NEW)) {
        PSAstNode *callee = parse_member_base(p);
        PSAstNode **args = NULL;
        size_t argc = 0;

        if (match(p, TOK_LPAREN)) {
            if (p->current.type != TOK_RPAREN) {
                do {
                    args = realloc(args, sizeof(PSAstNode *) * (argc + 1));
                    args[argc++] = parse_assignment(p);
                } while (match(p, TOK_COMMA));
            }
            expect(p, TOK_RPAREN, "')'");
        }

        return ps_ast_new(callee, args, argc);
    }

    return parse_primary_atom(p);
}

static PSAstNode *parse_member_base(PSParser *p) {
    PSAstNode *expr = parse_primary_atom(p);

    for (;;) {
        if (match(p, TOK_DOT)) {
            PSToken id = p->current;
            expect(p, TOK_IDENTIFIER, "property identifier");
            PSAstNode *prop = parse_identifier_token(id);
            expr = ps_ast_member(expr, prop, 0);
            continue;
        }

        if (match(p, TOK_LBRACKET)) {
            PSAstNode *prop = parse_expression(p);
            expect(p, TOK_RBRACKET, "']'");
            expr = ps_ast_member(expr, prop, 1);
            continue;
        }

        break;
    }

    return expr;
}

static PSAstNode *parse_primary_atom(PSParser *p) {
    PSToken tok = p->current;

    /* identifier */
    if (match(p, TOK_IDENTIFIER)) {
        return parse_identifier_token(tok);
    }

    /* number literal */
    if (match(p, TOK_NUMBER)) {
        return ps_ast_literal(ps_value_number(tok.number));
    }

    if (match(p, TOK_TRUE)) {
        return ps_ast_literal(ps_value_boolean(1));
    }

    if (match(p, TOK_FALSE)) {
        return ps_ast_literal(ps_value_boolean(0));
    }

    if (match(p, TOK_NULL)) {
        return ps_ast_literal(ps_value_null());
    }

    /* string literal */
    if (match(p, TOK_STRING)) {
        PSString *s = parse_string_literal(tok.start, tok.length);
        return ps_ast_literal(ps_value_string(s));
    }

    /* regex literal */
    if (p->current.type == TOK_SLASH) {
        return parse_regex_literal(p);
    }

    /* parenthesized */
    if (match(p, TOK_LPAREN)) {
        PSAstNode *expr = parse_expression(p);
        expect(p, TOK_RPAREN, "')'");
        return expr;
    }

    parse_error("Parse error: unexpected token");
    return ps_ast_literal(ps_value_undefined());
}

static PSAstNode *parse_regex_literal(PSParser *p) {
    const char *src = p->lexer.src;
    size_t start_pos = p->lexer.pos;
    size_t pos = start_pos;
    int in_class = 0;
    while (src[pos]) {
        char c = src[pos];
        if (c == '\n' || c == '\r') {
            parse_error("Parse error: unterminated regex literal");
        }
        if (c == '\\') {
            pos++;
            if (src[pos]) pos++;
            continue;
        }
        if (c == '[') {
            in_class = 1;
        } else if (c == ']') {
            in_class = 0;
        } else if (c == '/' && !in_class) {
            break;
        }
        pos++;
    }
    if (src[pos] != '/') {
        parse_error("Parse error: unterminated regex literal");
    }
    size_t pattern_len = pos - start_pos;
    pos++;
    size_t flags_start = pos;
    while (isalpha((unsigned char)src[pos])) {
        pos++;
    }
    size_t flags_len = pos - flags_start;
    p->lexer.pos = pos;
    advance(p);

    PSString *pattern = ps_string_from_utf8(src + start_pos, pattern_len);
    PSAstNode *callee = ps_ast_identifier("RegExp", 6);
    size_t argc = flags_len > 0 ? 2 : 1;
    PSAstNode **args = (PSAstNode **)malloc(sizeof(PSAstNode *) * argc);
    args[0] = ps_ast_literal(ps_value_string(pattern));
    if (flags_len > 0) {
        PSString *flags = ps_string_from_utf8(src + flags_start, flags_len);
        args[1] = ps_ast_literal(ps_value_string(flags));
    }
    return ps_ast_new(callee, args, argc);
}

static PSAstNode *parse_block(PSParser *p) {
    PSAstNode **items = NULL;
    size_t count = 0;

    while (p->current.type != TOK_RBRACE && p->current.type != TOK_EOF) {
        items = realloc(items, sizeof(PSAstNode *) * (count + 1));
        items[count++] = parse_statement(p);
    }

    expect(p, TOK_RBRACE, "'}'");
    return ps_ast_block(items, count);
}

static PSAstNode *parse_switch(PSParser *p) {
    expect(p, TOK_LPAREN, "'('");
    PSAstNode *expr = parse_expression(p);
    expect(p, TOK_RPAREN, "')'");
    expect(p, TOK_LBRACE, "'{'");

    PSAstNode **cases = NULL;
    size_t case_count = 0;

    while (p->current.type != TOK_RBRACE && p->current.type != TOK_EOF) {
        PSAstNode *test = NULL;
        if (match(p, TOK_CASE)) {
            test = parse_expression(p);
            expect(p, TOK_COLON, "':'");
        } else if (match(p, TOK_DEFAULT)) {
            test = NULL;
            expect(p, TOK_COLON, "':'");
        } else {
            parse_error("Parse error: expected case/default");
        }

        PSAstNode **items = NULL;
        size_t count = 0;
        while (p->current.type != TOK_CASE &&
               p->current.type != TOK_DEFAULT &&
               p->current.type != TOK_RBRACE) {
            items = realloc(items, sizeof(PSAstNode *) * (count + 1));
            items[count++] = parse_statement(p);
        }

        PSAstNode *case_node = ps_ast_case(test, items, count);
        cases = realloc(cases, sizeof(PSAstNode *) * (case_count + 1));
        cases[case_count++] = case_node;
    }

    expect(p, TOK_RBRACE, "'}'");
    return ps_ast_switch(expr, cases, case_count);
}
