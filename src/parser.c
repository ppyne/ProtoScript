#include "ps_lexer.h"
#include "ps_ast.h"
#include "ps_string.h"
#include "ps_config.h"

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <setjmp.h>

/* --------------------------------------------------------- */
/* Parser structure                                          */
/* --------------------------------------------------------- */

typedef struct {
    PSLexer  lexer;
    PSToken current;
    const char *source_path;
    struct PSIncludeStack *include_stack;
    int context_level;
    int saw_non_include;
} PSParser;

typedef struct PSIncludeStack {
    char **paths;
    size_t count;
} PSIncludeStack;

typedef struct {
    char **items;
    size_t count;
    size_t cap;
} PSSourcePathPool;

static jmp_buf *g_parse_jmp = NULL;
static PSParser *g_parse_parser = NULL;
static size_t g_parse_error_line = 0;
static size_t g_parse_error_column = 0;
static PSSourcePathPool g_source_paths = {0};

static const char *intern_source_path(const char *path) {
    if (!path || !path[0]) return NULL;
    for (size_t i = 0; i < g_source_paths.count; i++) {
        if (strcmp(g_source_paths.items[i], path) == 0) {
            return g_source_paths.items[i];
        }
    }
    char *copy = strdup(path);
    if (!copy) return NULL;
    if (g_source_paths.count == g_source_paths.cap) {
        size_t new_cap = g_source_paths.cap ? g_source_paths.cap * 2 : 8;
        char **next = (char **)realloc(g_source_paths.items, sizeof(char *) * new_cap);
        if (!next) {
            free(copy);
            return NULL;
        }
        g_source_paths.items = next;
        g_source_paths.cap = new_cap;
    }
    g_source_paths.items[g_source_paths.count++] = copy;
    return copy;
}

static void parse_error(const char *msg) {
    size_t line = 0;
    size_t column = 0;
    if (g_parse_parser) {
        if (g_parse_parser->lexer.error) {
            line = g_parse_parser->lexer.error_line;
            column = g_parse_parser->lexer.error_column;
        } else if (g_parse_error_line || g_parse_error_column) {
            line = g_parse_error_line;
            column = g_parse_error_column;
        } else {
            line = g_parse_parser->current.line;
            column = g_parse_parser->current.column;
        }
    }
    g_parse_error_line = 0;
    g_parse_error_column = 0;
    if (line > 0 && column > 0) {
        if (g_parse_parser && g_parse_parser->source_path) {
            fprintf(stderr, "%s:%zu:%zu %s\n", g_parse_parser->source_path, line, column, msg);
        } else {
            fprintf(stderr, "%zu:%zu %s\n", line, column, msg);
        }
    } else if (g_parse_parser && g_parse_parser->source_path) {
        fprintf(stderr, "%s: %s\n", g_parse_parser->source_path, msg);
    } else {
        fprintf(stderr, "%s\n", msg);
    }
    if (g_parse_jmp) {
        longjmp(*g_parse_jmp, 1);
    }
    exit(1);
}

static void parse_error_at(size_t line, size_t column, const char *msg) {
    g_parse_error_line = line;
    g_parse_error_column = column;
    parse_error(msg);
}

static PSAstNode *set_pos(PSAstNode *node, PSToken tok) {
    if (node) {
        node->line = tok.line;
        node->column = tok.column;
        node->source_path = g_parse_parser ? g_parse_parser->source_path : NULL;
    }
    return node;
}

static PSAstNode *set_pos_from_node(PSAstNode *node, PSAstNode *source) {
    if (node && source) {
        node->line = source->line;
        node->column = source->column;
        node->source_path = source->source_path;
    }
    return node;
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

static const char *token_repr(PSToken tok, char *buf, size_t buf_len) {
    if (!buf || buf_len == 0) return "token";
    buf[0] = '\0';
    switch (tok.type) {
        case TOK_EOF: return "end of input";
        case TOK_IDENTIFIER:
            if (tok.length > 0) {
                snprintf(buf, buf_len, "identifier '%.*s'", (int)tok.length, tok.start);
                return buf;
            }
            return "identifier";
        case TOK_NUMBER: return "number";
        case TOK_STRING: return "string literal";
        case TOK_VAR: return "'var'";
        case TOK_IF: return "'if'";
        case TOK_ELSE: return "'else'";
        case TOK_WHILE: return "'while'";
        case TOK_DO: return "'do'";
        case TOK_FOR: return "'for'";
        case TOK_IN: return "'in'";
        case TOK_OF: return "'of'";
        case TOK_SWITCH: return "'switch'";
        case TOK_CASE: return "'case'";
        case TOK_DEFAULT: return "'default'";
        case TOK_FUNCTION: return "'function'";
        case TOK_RETURN: return "'return'";
        case TOK_BREAK: return "'break'";
        case TOK_CONTINUE: return "'continue'";
        case TOK_WITH: return "'with'";
        case TOK_TRY: return "'try'";
        case TOK_CATCH: return "'catch'";
        case TOK_FINALLY: return "'finally'";
        case TOK_THROW: return "'throw'";
        case TOK_NEW: return "'new'";
        case TOK_INSTANCEOF: return "'instanceof'";
        case TOK_TRUE: return "'true'";
        case TOK_FALSE: return "'false'";
        case TOK_NULL: return "'null'";
        case TOK_THIS: return "'this'";
        case TOK_TYPEOF: return "'typeof'";
        case TOK_VOID: return "'void'";
        case TOK_DELETE: return "'delete'";
        case TOK_INCLUDE: return "'include'";
        case TOK_LPAREN: return "'('";
        case TOK_RPAREN: return "')'";
        case TOK_LBRACE: return "'{'";
        case TOK_RBRACE: return "'}'";
        case TOK_LBRACKET: return "'['";
        case TOK_RBRACKET: return "']'";
        case TOK_SEMI: return "';'";
        case TOK_COMMA: return "','";
        case TOK_DOT: return "'.'";
        case TOK_QUESTION: return "'?'";
        case TOK_COLON: return "':'";
        case TOK_ASSIGN: return "'='";
        case TOK_PLUS: return "'+'";
        case TOK_MINUS: return "'-'";
        case TOK_STAR: return "'*'";
        case TOK_SLASH: return "'/'";
        case TOK_PERCENT: return "'%'";
        case TOK_PLUS_PLUS: return "'++'";
        case TOK_MINUS_MINUS: return "'--'";
        case TOK_PLUS_ASSIGN: return "'+='";
        case TOK_MINUS_ASSIGN: return "'-='";
        case TOK_STAR_ASSIGN: return "'*='";
        case TOK_SLASH_ASSIGN: return "'/='";
        case TOK_PERCENT_ASSIGN: return "'%='";
        case TOK_LT: return "'<'";
        case TOK_LTE: return "'<='";
        case TOK_GT: return "'>'";
        case TOK_GTE: return "'>='";
        case TOK_EQ: return "'=='";
        case TOK_NEQ: return "'!='";
        case TOK_STRICT_EQ: return "'==='";
        case TOK_STRICT_NEQ: return "'!=='";
        case TOK_NOT: return "'!'";
        case TOK_BIT_NOT: return "'~'";
        case TOK_AND: return "'&'";
        case TOK_OR: return "'|'";
        case TOK_XOR: return "'^'";
        case TOK_AND_AND: return "'&&'";
        case TOK_OR_OR: return "'||'";
        case TOK_SHL: return "'<<'";
        case TOK_SHR: return "'>>'";
        case TOK_USHR: return "'>>>'";
        case TOK_AND_ASSIGN: return "'&='";
        case TOK_OR_ASSIGN: return "'|='";
        case TOK_XOR_ASSIGN: return "'^='";
        case TOK_SHL_ASSIGN: return "'<<='";
        case TOK_SHR_ASSIGN: return "'>>='";
        case TOK_USHR_ASSIGN: return "'>>>='";
        default:
            return "token";
    }
}

static void expect(PSParser *p, PSTokenType type, const char *msg) {
    if (!match(p, type)) {
        char buf[160];
        char tok_buf[64];
        const char *got = token_repr(p->current, tok_buf, sizeof(tok_buf));
        snprintf(buf, sizeof(buf), "Parse error: expected %s but found %s", msg, got);
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

static char *string_to_cstr(PSString *s) {
    if (!s) return NULL;
    if (memchr(s->utf8, '\0', s->byte_len)) {
        return NULL;
    }
    char *out = (char *)malloc(s->byte_len + 1);
    if (!out) return NULL;
    memcpy(out, s->utf8, s->byte_len);
    out[s->byte_len] = '\0';
    return out;
}

static int is_absolute_path(const char *path) {
    if (!path || !path[0]) return 0;
    if (path[0] == '/') return 1;
    if (path[0] == '\\' && path[1] == '\\') return 1;
    if (isalpha((unsigned char)path[0]) && path[1] == ':') return 1;
    return 0;
}

static int has_js_extension(const char *path) {
    if (!path) return 0;
    size_t len = strlen(path);
    return len >= 3 && strcmp(path + len - 3, ".js") == 0;
}

static char *resolve_include_path(const char *base_path, const char *include_path) {
    if (!include_path) return NULL;
    if (is_absolute_path(include_path)) {
        return strdup(include_path);
    }
    if (!base_path) {
        return NULL;
    }
    const char *slash = strrchr(base_path, '/');
    const char *bslash = strrchr(base_path, '\\');
    const char *sep = slash;
    if (bslash && (!sep || bslash > sep)) sep = bslash;
    size_t dir_len = sep ? (size_t)(sep - base_path) : 0;
    size_t inc_len = strlen(include_path);
    size_t out_len = dir_len + (dir_len ? 1 : 0) + inc_len;
    char *out = (char *)malloc(out_len + 1);
    if (!out) return NULL;
    if (dir_len) {
        memcpy(out, base_path, dir_len);
        out[dir_len] = '/';
        memcpy(out + dir_len + 1, include_path, inc_len);
    } else {
        memcpy(out, include_path, inc_len);
    }
    out[out_len] = '\0';
    return out;
}

static int include_stack_contains(PSIncludeStack *stack, const char *path, size_t *index) {
    if (!stack || !path) return 0;
    for (size_t i = 0; i < stack->count; i++) {
        if (strcmp(stack->paths[i], path) == 0) {
            if (index) *index = i;
            return 1;
        }
    }
    return 0;
}

static void include_stack_push(PSIncludeStack *stack, const char *path) {
    if (!stack || !path) return;
    char **next = (char **)realloc(stack->paths, sizeof(char *) * (stack->count + 1));
    if (!next) {
        parse_error("Parse error: out of memory");
    }
    stack->paths = next;
    stack->paths[stack->count] = strdup(path);
    if (!stack->paths[stack->count]) {
        parse_error("Parse error: out of memory");
    }
    stack->count++;
}

static void include_stack_pop(PSIncludeStack *stack) {
    if (!stack || stack->count == 0) return;
    free(stack->paths[stack->count - 1]);
    stack->count--;
}

static void include_cycle_error(PSIncludeStack *stack, const char *path) {
    if (!stack || !path) {
        parse_error("Include cycle detected");
        return;
    }
    size_t idx = 0;
    include_stack_contains(stack, path, &idx);
    size_t total = 0;
    for (size_t i = idx; i < stack->count; i++) {
        total += strlen(stack->paths[i]) + 4;
    }
    total += strlen(path) + 1;
    char *msg = (char *)malloc(total + 32);
    if (!msg) {
        parse_error("Include cycle detected");
        return;
    }
    strcpy(msg, "Include cycle detected: ");
    size_t pos = strlen(msg);
    for (size_t i = idx; i < stack->count; i++) {
        size_t len = strlen(stack->paths[i]);
        memcpy(msg + pos, stack->paths[i], len);
        pos += len;
        memcpy(msg + pos, " -> ", 4);
        pos += 4;
    }
    {
        size_t len = strlen(path);
        memcpy(msg + pos, path, len);
        pos += len;
    }
    msg[pos] = '\0';
    parse_error(msg);
}

static char *read_file(const char *path, size_t *out_len) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);
    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    size_t read = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    buf[read] = '\0';
    if (out_len) *out_len = read;
    return buf;
}

static char *decode_identifier(const char *start, size_t len, size_t *out_len, size_t line, size_t column) {
    size_t cap = len * 4 + 1;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        parse_error_at(line, column, "Parse error: out of memory");
    }
    size_t out = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)start[i];
        if (c != '\\') {
            buf[out++] = (char)c;
            continue;
        }
        if (i + 5 >= len || start[i + 1] != 'u') {
            free(buf);
            parse_error_at(line, column, "Parse error: invalid identifier escape");
        }
        int h1 = hex_value((unsigned char)start[i + 2]);
        int h2 = hex_value((unsigned char)start[i + 3]);
        int h3 = hex_value((unsigned char)start[i + 4]);
        int h4 = hex_value((unsigned char)start[i + 5]);
        if (h1 < 0 || h2 < 0 || h3 < 0 || h4 < 0) {
            free(buf);
            parse_error_at(line, column, "Parse error: invalid identifier escape");
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
        char *decoded = decode_identifier(tok.start, tok.length, &out_len, tok.line, tok.column);
        if (!decoded) {
            parse_error("Parse error: out of memory");
        }
        PSAstNode *node = ps_ast_identifier(decoded, out_len);
        set_pos(node, tok);
        free(decoded);
        return node;
    }
    return set_pos(ps_ast_identifier(tok.start, tok.length), tok);
}

static PSString *parse_object_key(PSToken tok) {
    if (memchr(tok.start, '\\', tok.length)) {
        size_t out_len = 0;
        char *decoded = decode_identifier(tok.start, tok.length, &out_len, tok.line, tok.column);
        if (!decoded) {
            parse_error("Parse error: out of memory");
        }
        PSString *key = ps_string_from_utf8(decoded, out_len);
        free(decoded);
        return key;
    }
    return ps_string_from_utf8(tok.start, tok.length);
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
static PSAstNode *parse_statement_nested(PSParser *p);
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
static PSAstNode *parse_regex_literal(PSParser *p, PSToken start_tok);
static PSAstNode *parse_array_literal(PSParser *p, PSToken start_tok);
static PSAstNode *parse_object_literal(PSParser *p, PSToken start_tok);
static PSAstNode *parse_member_base(PSParser *p);
static PSAstNode *parse_member(PSParser *p);
static PSAstNode *parse_block(PSParser *p, PSToken start_tok);
static PSAstNode *parse_switch(PSParser *p, PSToken switch_tok);
static PSAstNode *parse_include_statement(PSParser *p, PSToken include_tok);

/* --------------------------------------------------------- */
/* Entry point                                               */
/* --------------------------------------------------------- */

static PSAstNode *parse_source_with_path(const char *source, const char *path, PSIncludeStack *stack) {
    PSParser p;
    ps_lexer_init(&p.lexer, source);
    p.source_path = intern_source_path(path);
    p.include_stack = stack;
    p.context_level = 0;
    p.saw_non_include = 0;
    int pushed = 0;
    jmp_buf jmp_env;
    g_parse_jmp = &jmp_env;
    g_parse_parser = &p;
    if (setjmp(jmp_env) != 0) {
        if (pushed) {
            include_stack_pop(stack);
        }
        g_parse_jmp = NULL;
        g_parse_parser = NULL;
        return NULL;
    }

    if (path && stack) {
        include_stack_push(stack, path);
        pushed = 1;
    }

    advance(&p);

    PSAstNode **items = NULL;
    size_t count = 0;

    while (p.current.type != TOK_EOF) {
        items = realloc(items, sizeof(PSAstNode *) * (count + 1));
        items[count++] = parse_statement(&p);
    }

    if (pushed) {
        include_stack_pop(stack);
    }

    g_parse_jmp = NULL;
    g_parse_parser = NULL;
    return ps_ast_program(items, count);
}

PSAstNode *ps_parse_with_path(const char *source, const char *path) {
    PSIncludeStack stack = {0};
    PSAstNode *node = parse_source_with_path(source, path, &stack);
    for (size_t i = 0; i < stack.count; i++) {
        free(stack.paths[i]);
    }
    free(stack.paths);
    return node;
}

PSAstNode *ps_parse(const char *source) {
    return ps_parse_with_path(source, NULL);
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
        set_pos(decl, id);
        decls = realloc(decls, sizeof(PSAstNode *) * (count + 1));
        decls[count++] = decl;
    } while (match(p, TOK_COMMA));

    if (count == 1) {
        PSAstNode *only = decls[0];
        free(decls);
        return only;
    }

    PSAstNode *block = ps_ast_block(decls, count);
    return set_pos_from_node(block, count > 0 ? decls[0] : NULL);
}

static PSAstNode *parse_statement(PSParser *p) {
    int is_top_level = (p->context_level == 0);

    /* include directive */
    if (p->current.type == TOK_INCLUDE) {
        PSToken tok = p->current;
        if (!is_top_level) {
            parse_error_at(tok.line, tok.column, "Parse error: include is only allowed at top level");
        }
        if (p->saw_non_include) {
            parse_error_at(tok.line, tok.column, "Parse error: include must appear before any statements");
        }
        advance(p);
        PSAstNode *node = parse_include_statement(p, tok);
        expect(p, TOK_SEMI, "';'");
        return node;
    }

    if (is_top_level) {
        p->saw_non_include = 1;
    }

    /* label */
    if (p->current.type == TOK_IDENTIFIER) {
        PSParser saved = *p;
        PSToken label = p->current;
        advance(p);
        if (match(p, TOK_COLON)) {
            PSAstNode *label_node = parse_identifier_token(label);
            PSAstNode *stmt = parse_statement_nested(p);
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
                case AST_FOR_OF:
                    stmt->as.for_of.label = label_node;
                    return stmt;
                case AST_SWITCH:
                    stmt->as.switch_stmt.label = label_node;
                    return stmt;
                default:
                    return set_pos(ps_ast_label(label_node, stmt), label);
            }
        }
        *p = saved;
    }

    /* block */
    if (p->current.type == TOK_LBRACE) {
        PSToken tok = p->current;
        advance(p);
        return parse_block(p, tok);
    }

    /* var declaration */
    if (p->current.type == TOK_VAR) {
        PSToken tok = p->current;
        advance(p);
        PSAstNode *decls = parse_var_decl_list(p);
        expect(p, TOK_SEMI, "';'");
        return set_pos(decls, tok);
    }

    /* include directive */
    if (p->current.type == TOK_INCLUDE) {
        PSToken tok = p->current;
        advance(p);
        PSAstNode *node = parse_include_statement(p, tok);
        expect(p, TOK_SEMI, "';'");
        return node;
    }

    /* function declaration */
    if (p->current.type == TOK_FUNCTION) {
        PSToken tok = p->current;
        advance(p);
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

        PSToken body_tok = p->current;
        expect(p, TOK_LBRACE, "'{'");
        PSAstNode *body = parse_block(p, body_tok);
        return set_pos(ps_ast_func_decl(id_node, params, param_defaults, param_count, body), tok);
    }

    /* if statement */
    if (p->current.type == TOK_IF) {
        PSToken tok = p->current;
        advance(p);
        expect(p, TOK_LPAREN, "'('");
        PSAstNode *cond = parse_expression(p);
        expect(p, TOK_RPAREN, "')'");
        PSAstNode *then_branch = parse_statement_nested(p);
        PSAstNode *else_branch = NULL;
        if (match(p, TOK_ELSE)) {
            else_branch = parse_statement_nested(p);
        }
        return set_pos(ps_ast_if(cond, then_branch, else_branch), tok);
    }

    /* while statement */
    if (p->current.type == TOK_WHILE) {
        PSToken tok = p->current;
        advance(p);
        expect(p, TOK_LPAREN, "'('");
        PSAstNode *cond = parse_expression(p);
        expect(p, TOK_RPAREN, "')'");
        PSAstNode *body = parse_statement_nested(p);
        return set_pos(ps_ast_while(cond, body), tok);
    }

    /* do/while statement */
    if (p->current.type == TOK_DO) {
        PSToken tok = p->current;
        advance(p);
        PSAstNode *body = parse_statement_nested(p);
        expect(p, TOK_WHILE, "'while'");
        expect(p, TOK_LPAREN, "'('");
        PSAstNode *cond = parse_expression(p);
        expect(p, TOK_RPAREN, "')'");
        expect(p, TOK_SEMI, "';'");
        return set_pos(ps_ast_do_while(body, cond), tok);
    }

    /* for statement */
    if (p->current.type == TOK_FOR) {
        PSToken tok = p->current;
        advance(p);
        expect(p, TOK_LPAREN, "'('");
        PSParser saved = *p;

        if (match(p, TOK_VAR)) {
            PSToken id = p->current;
            expect(p, TOK_IDENTIFIER, "identifier");
            PSAstNode *id_node = parse_identifier_token(id);
            if (match(p, TOK_IN)) {
                PSAstNode *obj = parse_expression(p);
                expect(p, TOK_RPAREN, "')'");
                PSAstNode *body = parse_statement_nested(p);
                return set_pos(ps_ast_for_in(id_node, obj, body, 1), tok);
            }
            if (match(p, TOK_OF)) {
                PSAstNode *obj = parse_expression(p);
                expect(p, TOK_RPAREN, "')'");
                PSAstNode *body = parse_statement_nested(p);
                return set_pos(ps_ast_for_of(id_node, obj, body, 1), tok);
            }
        } else {
            PSAstNode *left = parse_member(p);
            if (match(p, TOK_IN)) {
                PSAstNode *obj = parse_expression(p);
                expect(p, TOK_RPAREN, "')'");
                PSAstNode *body = parse_statement_nested(p);
                return set_pos(ps_ast_for_in(left, obj, body, 0), tok);
            }
            if (match(p, TOK_OF)) {
                PSAstNode *obj = parse_expression(p);
                expect(p, TOK_RPAREN, "')'");
                PSAstNode *body = parse_statement_nested(p);
                return set_pos(ps_ast_for_of(left, obj, body, 0), tok);
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
            init = set_pos_from_node(ps_ast_expr_stmt(expr), expr);
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

        PSAstNode *body = parse_statement_nested(p);
        return set_pos(ps_ast_for(init, test, update, body), tok);
    }

    /* switch statement */
    if (p->current.type == TOK_SWITCH) {
        PSToken tok = p->current;
        advance(p);
        return parse_switch(p, tok);
    }

    /* break statement */
    if (p->current.type == TOK_BREAK) {
        PSToken tok = p->current;
        advance(p);
        PSAstNode *label = NULL;
        if (p->current.type == TOK_IDENTIFIER) {
            PSToken id = p->current;
            advance(p);
            label = parse_identifier_token(id);
        }
        expect(p, TOK_SEMI, "';'");
        return set_pos(ps_ast_break(label), tok);
    }

    /* continue statement */
    if (p->current.type == TOK_CONTINUE) {
        PSToken tok = p->current;
        advance(p);
        PSAstNode *label = NULL;
        if (p->current.type == TOK_IDENTIFIER) {
            PSToken id = p->current;
            advance(p);
            label = parse_identifier_token(id);
        }
        expect(p, TOK_SEMI, "';'");
        return set_pos(ps_ast_continue(label), tok);
    }

    /* with statement */
    if (p->current.type == TOK_WITH) {
#if !PS_ENABLE_WITH
        parse_error("Parse error: 'with' is disabled");
#endif
        PSToken tok = p->current;
        advance(p);
        expect(p, TOK_LPAREN, "'('");
        PSAstNode *obj = parse_expression(p);
        expect(p, TOK_RPAREN, "')'");
        PSAstNode *body = parse_statement_nested(p);
        return set_pos(ps_ast_with(obj, body), tok);
    }

    /* return statement */
    if (p->current.type == TOK_RETURN) {
        PSToken tok = p->current;
        advance(p);
        PSAstNode *expr = NULL;
        if (p->current.type != TOK_SEMI) {
            expr = parse_expression(p);
        }
        expect(p, TOK_SEMI, "';'");
        return set_pos(ps_ast_return(expr), tok);
    }

    /* throw statement */
    if (p->current.type == TOK_THROW) {
        PSToken tok = p->current;
        advance(p);
        PSAstNode *expr = parse_expression(p);
        expect(p, TOK_SEMI, "';'");
        return set_pos(ps_ast_throw(expr), tok);
    }

    /* try/catch/finally */
    if (p->current.type == TOK_TRY) {
        PSToken tok = p->current;
        advance(p);
        PSToken try_tok = p->current;
        expect(p, TOK_LBRACE, "'{'");
        PSAstNode *try_block = parse_block(p, try_tok);

        PSAstNode *catch_param = NULL;
        PSAstNode *catch_block = NULL;
        PSAstNode *finally_block = NULL;

        if (match(p, TOK_CATCH)) {
            expect(p, TOK_LPAREN, "'('");
            PSToken id = p->current;
            expect(p, TOK_IDENTIFIER, "identifier");
            catch_param = parse_identifier_token(id);
            expect(p, TOK_RPAREN, "')'");
            PSToken catch_tok = p->current;
            expect(p, TOK_LBRACE, "'{'");
            catch_block = parse_block(p, catch_tok);
        }

        if (match(p, TOK_FINALLY)) {
            PSToken finally_tok = p->current;
            expect(p, TOK_LBRACE, "'{'");
            finally_block = parse_block(p, finally_tok);
        }

        if (!catch_block && !finally_block) {
            parse_error("Parse error: try must have catch or finally");
        }

        return set_pos(ps_ast_try(try_block, catch_param, catch_block, finally_block), tok);
    }

    /* expression statement */
    PSAstNode *expr = parse_expression(p);
    expect(p, TOK_SEMI, "';'");
    return set_pos_from_node(ps_ast_expr_stmt(expr), expr);
}

static PSAstNode *parse_statement_nested(PSParser *p) {
    p->context_level++;
    PSAstNode *stmt = parse_statement(p);
    p->context_level--;
    return stmt;
}

static PSAstNode *parse_include_statement(PSParser *p, PSToken include_tok) {
    PSToken str_tok = p->current;
    expect(p, TOK_STRING, "string literal");
    PSString *raw = parse_string_literal(str_tok.start, str_tok.length);
    char *include_path = string_to_cstr(raw);
    if (!include_path) {
        parse_error_at(str_tok.line, str_tok.column, "Include error: invalid string literal");
    }
    if (!has_js_extension(include_path)) {
        free(include_path);
        parse_error_at(str_tok.line, str_tok.column, "Include error: path must end with .js");
    }
    char *resolved = resolve_include_path(p->source_path, include_path);
    if (!resolved) {
        free(include_path);
        parse_error_at(str_tok.line, str_tok.column, "Include error: cannot resolve path");
    }
    free(include_path);

    PSIncludeStack *stack = p->include_stack;
    if (stack && include_stack_contains(stack, resolved, NULL)) {
        include_cycle_error(stack, resolved);
    }

    struct stat st;
    if (stat(resolved, &st) != 0 || !S_ISREG(st.st_mode)) {
        free(resolved);
        parse_error_at(str_tok.line, str_tok.column, "Include error: file not found");
    }

    size_t len = 0;
    char *source = read_file(resolved, &len);
    if (!source) {
        free(resolved);
        parse_error_at(str_tok.line, str_tok.column, "Include error: unable to read file");
    }

    PSAstNode *program = parse_source_with_path(source, resolved, stack);
    free(source);
    free(resolved);
    if (!program || program->kind != AST_PROGRAM) {
        return set_pos(ps_ast_block(NULL, 0), include_tok);
    }
    PSAstNode *block = ps_ast_block(program->as.list.items, program->as.list.count);
    free(program);
    return set_pos(block, include_tok);
}

/* --------------------------------------------------------- */
/* Expressions                                               */
/* --------------------------------------------------------- */

static PSAstNode *parse_expression(PSParser *p) {
    return parse_comma(p);
}

static PSAstNode *parse_comma(PSParser *p) {
    PSAstNode *left = parse_assignment(p);
    while (p->current.type == TOK_COMMA) {
        PSToken tok = p->current;
        advance(p);
        PSAstNode *right = parse_assignment(p);
        left = set_pos(ps_ast_binary(TOK_COMMA, left, right), tok);
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
        PSToken tok = p->current;
        int op = p->current.type;
        advance(p);
        if (left->kind != AST_IDENTIFIER && left->kind != AST_MEMBER) {
            parse_error("Parse error: invalid assignment target");
        }
        PSAstNode *value = parse_assignment(p);
        return set_pos(ps_ast_assign(op, left, value), tok);
    }

    return left;
}

static PSAstNode *parse_conditional(PSParser *p) {
    PSAstNode *cond = parse_logical_or(p);
    if (p->current.type == TOK_QUESTION) {
        PSToken tok = p->current;
        advance(p);
        PSAstNode *then_expr = parse_assignment(p);
        expect(p, TOK_COLON, "':'");
        PSAstNode *else_expr = parse_assignment(p);
        return set_pos(ps_ast_conditional(cond, then_expr, else_expr), tok);
    }
    return cond;
}

static PSAstNode *parse_logical_or(PSParser *p) {
    PSAstNode *left = parse_logical_and(p);
    while (p->current.type == TOK_OR_OR) {
        PSToken tok = p->current;
        int op = p->current.type;
        advance(p);
        PSAstNode *right = parse_logical_and(p);
        left = set_pos(ps_ast_binary(op, left, right), tok);
    }
    return left;
}

static PSAstNode *parse_logical_and(PSParser *p) {
    PSAstNode *left = parse_bitwise_or(p);
    while (p->current.type == TOK_AND_AND) {
        PSToken tok = p->current;
        int op = p->current.type;
        advance(p);
        PSAstNode *right = parse_bitwise_or(p);
        left = set_pos(ps_ast_binary(op, left, right), tok);
    }
    return left;
}

static PSAstNode *parse_bitwise_or(PSParser *p) {
    PSAstNode *left = parse_bitwise_xor(p);
    while (p->current.type == TOK_OR) {
        PSToken tok = p->current;
        int op = p->current.type;
        advance(p);
        PSAstNode *right = parse_bitwise_xor(p);
        left = set_pos(ps_ast_binary(op, left, right), tok);
    }
    return left;
}

static PSAstNode *parse_bitwise_xor(PSParser *p) {
    PSAstNode *left = parse_bitwise_and(p);
    while (p->current.type == TOK_XOR) {
        PSToken tok = p->current;
        int op = p->current.type;
        advance(p);
        PSAstNode *right = parse_bitwise_and(p);
        left = set_pos(ps_ast_binary(op, left, right), tok);
    }
    return left;
}

static PSAstNode *parse_bitwise_and(PSParser *p) {
    PSAstNode *left = parse_equality(p);
    while (p->current.type == TOK_AND) {
        PSToken tok = p->current;
        int op = p->current.type;
        advance(p);
        PSAstNode *right = parse_equality(p);
        left = set_pos(ps_ast_binary(op, left, right), tok);
    }
    return left;
}

static PSAstNode *parse_equality(PSParser *p) {
    PSAstNode *left = parse_relational(p);
    while (p->current.type == TOK_EQ ||
           p->current.type == TOK_NEQ ||
           p->current.type == TOK_STRICT_EQ ||
           p->current.type == TOK_STRICT_NEQ) {
        PSToken tok = p->current;
        int op = p->current.type;
        advance(p);
        PSAstNode *right = parse_relational(p);
        left = set_pos(ps_ast_binary(op, left, right), tok);
    }
    return left;
}

static PSAstNode *parse_relational(PSParser *p) {
    PSAstNode *left = parse_shift(p);

    while (p->current.type == TOK_LT ||
           p->current.type == TOK_LTE ||
           p->current.type == TOK_GT ||
           p->current.type == TOK_GTE ||
           p->current.type == TOK_INSTANCEOF ||
           p->current.type == TOK_IN) {
        PSToken tok = p->current;
        int op = p->current.type;
        advance(p);
        PSAstNode *right = parse_shift(p);
        left = set_pos(ps_ast_binary(op, left, right), tok);
    }

    return left;
}

static PSAstNode *parse_shift(PSParser *p) {
    PSAstNode *left = parse_additive(p);
    while (p->current.type == TOK_SHL ||
           p->current.type == TOK_SHR ||
           p->current.type == TOK_USHR) {
        PSToken tok = p->current;
        int op = p->current.type;
        advance(p);
        PSAstNode *right = parse_additive(p);
        left = set_pos(ps_ast_binary(op, left, right), tok);
    }
    return left;
}

static PSAstNode *parse_additive(PSParser *p) {
    PSAstNode *left = parse_multiplicative(p);

    while (p->current.type == TOK_PLUS || p->current.type == TOK_MINUS) {
        PSToken tok = p->current;
        int op = p->current.type;
        advance(p);
        PSAstNode *right = parse_multiplicative(p);
        left = set_pos(ps_ast_binary(op, left, right), tok);
    }

    return left;
}

static PSAstNode *parse_multiplicative(PSParser *p) {
    PSAstNode *left = parse_unary(p);
    while (p->current.type == TOK_STAR ||
           p->current.type == TOK_SLASH ||
           p->current.type == TOK_PERCENT) {
        PSToken tok = p->current;
        int op = p->current.type;
        advance(p);
        PSAstNode *right = parse_unary(p);
        left = set_pos(ps_ast_binary(op, left, right), tok);
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
        PSToken tok = p->current;
        int op = p->current.type;
        advance(p);
        PSAstNode *expr = parse_unary(p);
        return set_pos(ps_ast_unary(op, expr), tok);
    }

    if (p->current.type == TOK_PLUS_PLUS ||
        p->current.type == TOK_MINUS_MINUS) {
        PSToken tok = p->current;
        int op = p->current.type;
        advance(p);
        PSAstNode *expr = parse_unary(p);
        return set_pos(ps_ast_update(op, 1, expr), tok);
    }

    return parse_postfix(p);
}

static PSAstNode *parse_postfix(PSParser *p) {
    PSAstNode *expr = parse_member(p);
    if (p->current.type == TOK_PLUS_PLUS ||
        p->current.type == TOK_MINUS_MINUS) {
        PSToken tok = p->current;
        int op = p->current.type;
        advance(p);
        return set_pos(ps_ast_update(op, 0, expr), tok);
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
        if (p->current.type == TOK_DOT) {
            PSToken tok = p->current;
            advance(p);
            PSToken id = p->current;
            expect(p, TOK_IDENTIFIER, "property identifier");
            PSAstNode *prop =
                parse_identifier_token(id);
            expr = set_pos(ps_ast_member(expr, prop, 0), tok);
            continue;
        }

        /* obj[expr] */
        if (p->current.type == TOK_LBRACKET) {
            PSToken tok = p->current;
            advance(p);
            PSAstNode *prop = parse_expression(p);
            expect(p, TOK_RBRACKET, "']'");
            expr = set_pos(ps_ast_member(expr, prop, 1), tok);
            continue;
        }

        /* call */
        if (p->current.type == TOK_LPAREN) {
            PSToken tok = p->current;
            advance(p);
            PSAstNode **args = NULL;
            size_t argc = 0;

            if (p->current.type != TOK_RPAREN) {
                do {
                    args = realloc(args, sizeof(PSAstNode *) * (argc + 1));
                    args[argc++] = parse_assignment(p);
                } while (match(p, TOK_COMMA));
            }

            expect(p, TOK_RPAREN, "')'");
            expr = set_pos(ps_ast_call(expr, args, argc), tok);
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
    if (p->current.type == TOK_NEW) {
        PSToken tok = p->current;
        advance(p);
        PSAstNode *callee = parse_member_base(p);
        PSAstNode **args = NULL;
        size_t argc = 0;

        if (p->current.type == TOK_LPAREN) {
            advance(p);
            if (p->current.type != TOK_RPAREN) {
                do {
                    args = realloc(args, sizeof(PSAstNode *) * (argc + 1));
                    args[argc++] = parse_assignment(p);
                } while (match(p, TOK_COMMA));
            }
            expect(p, TOK_RPAREN, "')'");
        }

        return set_pos(ps_ast_new(callee, args, argc), tok);
    }

    return parse_primary_atom(p);
}

static PSAstNode *parse_member_base(PSParser *p) {
    PSAstNode *expr = parse_primary_atom(p);

    for (;;) {
        if (p->current.type == TOK_DOT) {
            PSToken tok = p->current;
            advance(p);
            PSToken id = p->current;
            expect(p, TOK_IDENTIFIER, "property identifier");
            PSAstNode *prop = parse_identifier_token(id);
            expr = set_pos(ps_ast_member(expr, prop, 0), tok);
            continue;
        }

        if (p->current.type == TOK_LBRACKET) {
            PSToken tok = p->current;
            advance(p);
            PSAstNode *prop = parse_expression(p);
            expect(p, TOK_RBRACKET, "']'");
            expr = set_pos(ps_ast_member(expr, prop, 1), tok);
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

    if (match(p, TOK_THIS)) {
        return set_pos(ps_ast_this(), tok);
    }

    /* number literal */
    if (match(p, TOK_NUMBER)) {
        return set_pos(ps_ast_literal(ps_value_number(tok.number)), tok);
    }

    if (match(p, TOK_TRUE)) {
        return set_pos(ps_ast_literal(ps_value_boolean(1)), tok);
    }

    if (match(p, TOK_FALSE)) {
        return set_pos(ps_ast_literal(ps_value_boolean(0)), tok);
    }

    if (match(p, TOK_NULL)) {
        return set_pos(ps_ast_literal(ps_value_null()), tok);
    }

    /* function expression */
    if (match(p, TOK_FUNCTION)) {
        PSToken func_tok = tok;
        PSAstNode *id_node = NULL;
        if (p->current.type == TOK_IDENTIFIER) {
            PSToken id = p->current;
            advance(p);
            id_node = parse_identifier_token(id);
        }

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

        PSToken body_tok = p->current;
        expect(p, TOK_LBRACE, "'{'");
        PSAstNode *body = parse_block(p, body_tok);
        return set_pos(ps_ast_func_expr(id_node, params, param_defaults, param_count, body), func_tok);
    }

    /* string literal */
    if (match(p, TOK_STRING)) {
        PSString *s = parse_string_literal(tok.start, tok.length);
        return set_pos(ps_ast_literal(ps_value_string(s)), tok);
    }

    /* array literal */
    if (match(p, TOK_LBRACKET)) {
        return parse_array_literal(p, tok);
    }

    /* object literal */
    if (match(p, TOK_LBRACE)) {
        return parse_object_literal(p, tok);
    }

    /* regex literal */
    if (p->current.type == TOK_SLASH) {
        return parse_regex_literal(p, tok);
    }

    /* parenthesized */
    if (match(p, TOK_LPAREN)) {
        PSAstNode *expr = parse_expression(p);
        expect(p, TOK_RPAREN, "')'");
        return expr;
    }

    parse_error("Parse error: unexpected token");
    return set_pos(ps_ast_literal(ps_value_undefined()), tok);
}

static PSAstNode *parse_array_literal(PSParser *p, PSToken start_tok) {
    PSAstNode **items = NULL;
    size_t count = 0;

    if (match(p, TOK_RBRACKET)) {
        return set_pos(ps_ast_array_literal(items, count), start_tok);
    }

    while (1) {
        if (match(p, TOK_COMMA)) {
            items = realloc(items, sizeof(PSAstNode *) * (count + 1));
            items[count++] = NULL;
            if (match(p, TOK_RBRACKET)) {
                break;
            }
            continue;
        }
        if (match(p, TOK_RBRACKET)) {
            break;
        }
        items = realloc(items, sizeof(PSAstNode *) * (count + 1));
        items[count++] = parse_assignment(p);

        if (match(p, TOK_COMMA)) {
            if (match(p, TOK_RBRACKET)) {
                break;
            }
            continue;
        }
        expect(p, TOK_RBRACKET, "']'");
        break;
    }

    return set_pos(ps_ast_array_literal(items, count), start_tok);
}

static PSAstNode *parse_object_literal(PSParser *p, PSToken start_tok) {
    PSAstProperty *props = NULL;
    size_t count = 0;

    if (match(p, TOK_RBRACE)) {
        return set_pos(ps_ast_object_literal(props, count), start_tok);
    }

    while (1) {
        PSString *key = NULL;
        if (p->current.type == TOK_IDENTIFIER) {
            PSToken key_tok = p->current;
            match(p, TOK_IDENTIFIER);
            key = parse_object_key(key_tok);
        } else if (p->current.type == TOK_STRING) {
            PSToken key_tok = p->current;
            match(p, TOK_STRING);
            key = parse_string_literal(key_tok.start, key_tok.length);
        } else {
            parse_error("Parse error: expected object key");
        }

        expect(p, TOK_COLON, "':'");
        PSAstNode *value = parse_assignment(p);

        props = realloc(props, sizeof(PSAstProperty) * (count + 1));
        props[count].key = key;
        props[count].value = value;
        count++;

        if (match(p, TOK_COMMA)) {
            if (match(p, TOK_RBRACE)) {
                break;
            }
            continue;
        }
        expect(p, TOK_RBRACE, "'}'");
        break;
    }

    return set_pos(ps_ast_object_literal(props, count), start_tok);
}

static PSAstNode *parse_regex_literal(PSParser *p, PSToken start_tok) {
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
    set_pos(callee, start_tok);
    size_t argc = flags_len > 0 ? 2 : 1;
    PSAstNode **args = (PSAstNode **)malloc(sizeof(PSAstNode *) * argc);
    args[0] = set_pos(ps_ast_literal(ps_value_string(pattern)), start_tok);
    if (flags_len > 0) {
        PSString *flags = ps_string_from_utf8(src + flags_start, flags_len);
        args[1] = set_pos(ps_ast_literal(ps_value_string(flags)), start_tok);
    }
    return set_pos(ps_ast_new(callee, args, argc), start_tok);
}

static PSAstNode *parse_block(PSParser *p, PSToken start_tok) {
    PSAstNode **items = NULL;
    size_t count = 0;
    int saved_level = p->context_level;
    p->context_level++;

    while (p->current.type != TOK_RBRACE && p->current.type != TOK_EOF) {
        items = realloc(items, sizeof(PSAstNode *) * (count + 1));
        items[count++] = parse_statement(p);
    }

    expect(p, TOK_RBRACE, "'}'");
    p->context_level = saved_level;
    return set_pos(ps_ast_block(items, count), start_tok);
}

static PSAstNode *parse_switch(PSParser *p, PSToken switch_tok) {
    expect(p, TOK_LPAREN, "'('");
    PSAstNode *expr = parse_expression(p);
    expect(p, TOK_RPAREN, "')'");
    expect(p, TOK_LBRACE, "'{'");

    PSAstNode **cases = NULL;
    size_t case_count = 0;

    while (p->current.type != TOK_RBRACE && p->current.type != TOK_EOF) {
        PSAstNode *test = NULL;
        if (p->current.type == TOK_CASE) {
            PSToken case_tok = p->current;
            advance(p);
            test = parse_expression(p);
            expect(p, TOK_COLON, "':'");
            PSAstNode **items = NULL;
            size_t count = 0;
            while (p->current.type != TOK_CASE &&
                   p->current.type != TOK_DEFAULT &&
                   p->current.type != TOK_RBRACE) {
                items = realloc(items, sizeof(PSAstNode *) * (count + 1));
                items[count++] = parse_statement_nested(p);
            }

            PSAstNode *case_node = set_pos(ps_ast_case(test, items, count), case_tok);
            cases = realloc(cases, sizeof(PSAstNode *) * (case_count + 1));
            cases[case_count++] = case_node;
            continue;
        } else if (p->current.type == TOK_DEFAULT) {
            PSToken case_tok = p->current;
            advance(p);
            test = NULL;
            expect(p, TOK_COLON, "':'");
            PSAstNode **items = NULL;
            size_t count = 0;
            while (p->current.type != TOK_CASE &&
                   p->current.type != TOK_DEFAULT &&
                   p->current.type != TOK_RBRACE) {
                items = realloc(items, sizeof(PSAstNode *) * (count + 1));
                items[count++] = parse_statement_nested(p);
            }

            PSAstNode *case_node = set_pos(ps_ast_case(test, items, count), case_tok);
            cases = realloc(cases, sizeof(PSAstNode *) * (case_count + 1));
            cases[case_count++] = case_node;
            continue;
        } else {
            parse_error("Parse error: expected case/default");
        }
    }

    expect(p, TOK_RBRACE, "'}'");
    return set_pos(ps_ast_switch(expr, cases, case_count), switch_tok);
}
