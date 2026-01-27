#ifndef PS_AST_H
#define PS_AST_H

#include <stddef.h>
#include <stdint.h>
#include "ps_value.h"
#include "ps_string.h"

/* --------------------------------------------------------- */
/* AST node kinds                                            */
/* --------------------------------------------------------- */

typedef enum {
    /* Program */
    AST_PROGRAM,

    /* Statements */
    AST_BLOCK,
    AST_VAR_DECL,
    AST_EXPR_STMT,
    AST_RETURN,
    AST_IF,
    AST_WHILE,
    AST_DO_WHILE,
    AST_FOR,
    AST_FOR_IN,
    AST_FOR_OF,
    AST_SWITCH,
    AST_CASE,
    AST_LABEL,
    AST_BREAK,
    AST_CONTINUE,
    AST_WITH,
    AST_THROW,
    AST_TRY,
    AST_FUNCTION_DECL,
    AST_FUNCTION_EXPR,

    /* Expressions */
    AST_IDENTIFIER,
    AST_THIS,
    AST_LITERAL,
    AST_ASSIGN,
    AST_BINARY,
    AST_UNARY,
    AST_UPDATE,
    AST_CONDITIONAL,
    AST_CALL,
    AST_MEMBER,
    AST_NEW,
    AST_ARRAY_LITERAL,
    AST_OBJECT_LITERAL
} PSAstKind;

#define PS_AST_KIND_COUNT (AST_OBJECT_LITERAL + 1)

/* --------------------------------------------------------- */
/* Forward declarations                                      */
/* --------------------------------------------------------- */

typedef struct PSAstNode PSAstNode;
struct PSObject;
struct PSProperty;
struct PSEnv;
struct PSExprBC;

/* --------------------------------------------------------- */
/* AST node definition                                       */
/* --------------------------------------------------------- */

struct PSAstNode {
    PSAstKind kind;
    size_t line;
    size_t column;
    const char *source_path;
    struct PSExprBC *expr_bc;
    uint8_t expr_bc_state;

    union {
        /* Program / Block */
        struct {
            PSAstNode **items;
            size_t      count;
        } list;

        /* var x = expr; */
        struct {
            PSAstNode *id;
            PSAstNode *init; /* may be NULL */
        } var_decl;

        /* expression; */
        struct {
            PSAstNode *expr;
        } expr_stmt;

        /* return expr; */
        struct {
            PSAstNode *expr; /* may be NULL */
        } ret;

        /* if (cond) then else */
        struct {
            PSAstNode *cond;
            PSAstNode *then_branch;
            PSAstNode *else_branch; /* may be NULL */
        } if_stmt;

        /* while (cond) body */
        struct {
            PSAstNode *cond;
            PSAstNode *body;
            PSAstNode *label; /* optional label */
        } while_stmt;

        /* do body while (cond); */
        struct {
            PSAstNode *body;
            PSAstNode *cond;
            PSAstNode *label; /* optional label */
        } do_while;

        /* for (init; test; update) body */
        struct {
            PSAstNode *init;   /* may be NULL */
            PSAstNode *test;   /* may be NULL */
            PSAstNode *update; /* may be NULL */
            PSAstNode *body;
            PSAstNode *label;  /* optional label */
        } for_stmt;

        /* for (target in object) body */
        struct {
            PSAstNode *target;
            PSAstNode *object;
            PSAstNode *body;
            int        is_var;
            PSAstNode *label;  /* optional label */
        } for_in;

        /* for (target of object) body */
        struct {
            PSAstNode *target;
            PSAstNode *object;
            PSAstNode *body;
            int        is_var;
            PSAstNode *label;  /* optional label */
        } for_of;

        /* switch (expr) { cases } */
        struct {
            PSAstNode  *expr;
            PSAstNode **cases;
            size_t      case_count;
            PSAstNode  *label; /* optional label */
        } switch_stmt;

        /* case expr: ... / default: ... */
        struct {
            PSAstNode  *test; /* NULL for default */
            PSAstNode **items;
            size_t      count;
        } case_stmt;

        /* label: statement */
        struct {
            PSAstNode *label;
            PSAstNode *stmt;
        } label_stmt;

        /* break / continue */
        struct {
            PSAstNode *label; /* may be NULL */
        } jump_stmt;

        /* with (object) body */
        struct {
            PSAstNode *object;
            PSAstNode *body;
        } with_stmt;

        /* throw expr; */
        struct {
            PSAstNode *expr;
        } throw_stmt;

        /* try { } catch (id) { } finally { } */
        struct {
            PSAstNode *try_block;
            PSAstNode *catch_param; /* identifier or NULL */
            PSAstNode *catch_block; /* block or NULL */
            PSAstNode *finally_block; /* block or NULL */
        } try_stmt;

        /* function name(params) { body } */
        struct {
            PSAstNode  *id;
            PSAstNode **params;
            PSAstNode **param_defaults;
            size_t      param_count;
            PSAstNode  *body;
        } func_decl;

        /* function (params) { body } */
        struct {
            PSAstNode  *id; /* optional name */
            PSAstNode **params;
            PSAstNode **param_defaults;
            size_t      param_count;
            PSAstNode  *body;
        } func_expr;

        /* identifier */
        struct {
            const char *name;
            size_t      length;
            PSString   *str;
            struct PSEnv *cache_fast_env;
            size_t        cache_fast_index;
            struct PSEnv *cache_env;
            struct PSObject *cache_record;
            struct PSProperty *cache_prop;
            uint32_t      cache_shape;
            uint8_t       fast_num_kind;
            uint32_t      fast_num_index;
        } identifier;

        /* literal (number, string, boolean, null, undefined) */
        struct {
            PSValue value;
        } literal;

        /* binary expression (a + b, etc.) */
        struct {
            int        op; /* token or internal enum */
            PSAstNode *left;
            PSAstNode *right;
        } binary;

        /* assignment (target = value) */
        struct {
            int        op; /* TOK_ASSIGN or compound */
            PSAstNode *target;
            PSAstNode *value;
        } assign;

        /* unary expression (!a, -a, typeof a, delete a) */
        struct {
            int        op;
            PSAstNode *expr;
        } unary;

        /* update expression (++a, a--) */
        struct {
            int        op;
            int        is_prefix;
            PSAstNode *expr;
        } update;

        /* conditional (a ? b : c) */
        struct {
            PSAstNode *cond;
            PSAstNode *then_expr;
            PSAstNode *else_expr;
        } conditional;

        /* function call */
        struct {
            PSAstNode  *callee;
            PSAstNode **args;
            size_t      argc;
            uint8_t     fast_num_math_id;
            uint8_t     cache_kind;
            struct PSEnv *cache_env;
            struct PSEnv *cache_fast_env;
            size_t      cache_fast_index;
            struct PSObject *cache_record;
            struct PSProperty *cache_prop;
            uint32_t    cache_shape;
            struct PSObject *cache_obj;
            struct PSProperty *cache_member_prop;
            uint32_t    cache_member_shape;
        } call;

        /* member access: obj.prop */
        struct {
            PSAstNode *object;
            PSAstNode *property;
            int        computed;
            struct PSObject *cache_obj;
            struct PSProperty *cache_prop;
            uint32_t   cache_shape;
        } member;

        /* new callee(args) */
        struct {
            PSAstNode  *callee;
            PSAstNode **args;
            size_t      argc;
        } new_expr;

        /* array literal [a, b, c] */
        struct {
            PSAstNode **items;
            size_t      count;
        } array_literal;

        /* object literal { key: value } */
        struct {
            struct PSAstProperty *props;
            size_t                count;
        } object_literal;

    } as;
};

typedef struct PSAstProperty {
    PSString  *key;
    PSAstNode *value;
} PSAstProperty;

/* --------------------------------------------------------- */
/* Constructors                                              */
/* --------------------------------------------------------- */

PSAstNode *ps_ast_program(PSAstNode **items, size_t count);
PSAstNode *ps_ast_block(PSAstNode **items, size_t count);

PSAstNode *ps_ast_var_decl(PSAstNode *id, PSAstNode *init);
PSAstNode *ps_ast_expr_stmt(PSAstNode *expr);
PSAstNode *ps_ast_return(PSAstNode *expr);
PSAstNode *ps_ast_if(PSAstNode *cond, PSAstNode *then_branch, PSAstNode *else_branch);
PSAstNode *ps_ast_while(PSAstNode *cond, PSAstNode *body);
PSAstNode *ps_ast_do_while(PSAstNode *body, PSAstNode *cond);
PSAstNode *ps_ast_for(PSAstNode *init, PSAstNode *test, PSAstNode *update, PSAstNode *body);
PSAstNode *ps_ast_for_in(PSAstNode *target, PSAstNode *object, PSAstNode *body, int is_var);
PSAstNode *ps_ast_for_of(PSAstNode *target, PSAstNode *object, PSAstNode *body, int is_var);
PSAstNode *ps_ast_switch(PSAstNode *expr, PSAstNode **cases, size_t case_count);
PSAstNode *ps_ast_case(PSAstNode *test, PSAstNode **items, size_t count);
PSAstNode *ps_ast_label(PSAstNode *label, PSAstNode *stmt);
PSAstNode *ps_ast_break(PSAstNode *label);
PSAstNode *ps_ast_continue(PSAstNode *label);
PSAstNode *ps_ast_with(PSAstNode *object, PSAstNode *body);
PSAstNode *ps_ast_throw(PSAstNode *expr);
PSAstNode *ps_ast_try(PSAstNode *try_block,
                      PSAstNode *catch_param,
                      PSAstNode *catch_block,
                      PSAstNode *finally_block);
PSAstNode *ps_ast_func_decl(PSAstNode *id,
                            PSAstNode **params,
                            PSAstNode **param_defaults,
                            size_t param_count,
                            PSAstNode *body);
PSAstNode *ps_ast_func_expr(PSAstNode *id,
                            PSAstNode **params,
                            PSAstNode **param_defaults,
                            size_t param_count,
                            PSAstNode *body);

PSAstNode *ps_ast_identifier(const char *name, size_t length);
PSAstNode *ps_ast_this(void);
PSAstNode *ps_ast_literal(PSValue value);

PSAstNode *ps_ast_assign(int op, PSAstNode *target, PSAstNode *value);
PSAstNode *ps_ast_binary(int op, PSAstNode *left, PSAstNode *right);
PSAstNode *ps_ast_unary(int op, PSAstNode *expr);
PSAstNode *ps_ast_update(int op, int is_prefix, PSAstNode *expr);
PSAstNode *ps_ast_conditional(PSAstNode *cond, PSAstNode *then_expr, PSAstNode *else_expr);
PSAstNode *ps_ast_call(PSAstNode *callee, PSAstNode **args, size_t argc);
PSAstNode *ps_ast_member(PSAstNode *object, PSAstNode *property, int computed);
PSAstNode *ps_ast_new(PSAstNode *callee, PSAstNode **args, size_t argc);
PSAstNode *ps_ast_array_literal(PSAstNode **items, size_t count);
PSAstNode *ps_ast_object_literal(PSAstProperty *props, size_t count);

/* --------------------------------------------------------- */
/* Destruction                                               */
/* --------------------------------------------------------- */

void ps_ast_free(PSAstNode *node);

#endif /* PS_AST_H */
