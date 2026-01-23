#include "ps_ast.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* --------------------------------------------------------- */
/* Helpers                                                   */
/* --------------------------------------------------------- */

static PSAstNode *alloc_node(PSAstKind kind) {
    PSAstNode *n = (PSAstNode *)calloc(1, sizeof(PSAstNode));
    if (!n) return NULL;
    n->kind = kind;
    return n;
}

/* --------------------------------------------------------- */
/* Constructors                                              */
/* --------------------------------------------------------- */

PSAstNode *ps_ast_program(PSAstNode **items, size_t count) {
    PSAstNode *n = alloc_node(AST_PROGRAM);
    n->as.list.items = items;
    n->as.list.count = count;
    return n;
}

PSAstNode *ps_ast_block(PSAstNode **items, size_t count) {
    PSAstNode *n = alloc_node(AST_BLOCK);
    n->as.list.items = items;
    n->as.list.count = count;
    return n;
}

PSAstNode *ps_ast_var_decl(PSAstNode *id, PSAstNode *init) {
    PSAstNode *n = alloc_node(AST_VAR_DECL);
    n->as.var_decl.id = id;
    n->as.var_decl.init = init;
    return n;
}

PSAstNode *ps_ast_expr_stmt(PSAstNode *expr) {
    PSAstNode *n = alloc_node(AST_EXPR_STMT);
    n->as.expr_stmt.expr = expr;
    return n;
}

PSAstNode *ps_ast_return(PSAstNode *expr) {
    PSAstNode *n = alloc_node(AST_RETURN);
    n->as.ret.expr = expr;
    return n;
}

PSAstNode *ps_ast_if(PSAstNode *cond, PSAstNode *then_branch, PSAstNode *else_branch) {
    PSAstNode *n = alloc_node(AST_IF);
    n->as.if_stmt.cond = cond;
    n->as.if_stmt.then_branch = then_branch;
    n->as.if_stmt.else_branch = else_branch;
    return n;
}

PSAstNode *ps_ast_while(PSAstNode *cond, PSAstNode *body) {
    PSAstNode *n = alloc_node(AST_WHILE);
    n->as.while_stmt.cond = cond;
    n->as.while_stmt.body = body;
    n->as.while_stmt.label = NULL;
    return n;
}

PSAstNode *ps_ast_do_while(PSAstNode *body, PSAstNode *cond) {
    PSAstNode *n = alloc_node(AST_DO_WHILE);
    n->as.do_while.body = body;
    n->as.do_while.cond = cond;
    n->as.do_while.label = NULL;
    return n;
}

PSAstNode *ps_ast_for(PSAstNode *init, PSAstNode *test, PSAstNode *update, PSAstNode *body) {
    PSAstNode *n = alloc_node(AST_FOR);
    n->as.for_stmt.init = init;
    n->as.for_stmt.test = test;
    n->as.for_stmt.update = update;
    n->as.for_stmt.body = body;
    n->as.for_stmt.label = NULL;
    return n;
}

PSAstNode *ps_ast_for_in(PSAstNode *target, PSAstNode *object, PSAstNode *body, int is_var) {
    PSAstNode *n = alloc_node(AST_FOR_IN);
    n->as.for_in.target = target;
    n->as.for_in.object = object;
    n->as.for_in.body = body;
    n->as.for_in.is_var = is_var;
    n->as.for_in.label = NULL;
    return n;
}

PSAstNode *ps_ast_for_of(PSAstNode *target, PSAstNode *object, PSAstNode *body, int is_var) {
    PSAstNode *n = alloc_node(AST_FOR_OF);
    n->as.for_of.target = target;
    n->as.for_of.object = object;
    n->as.for_of.body = body;
    n->as.for_of.is_var = is_var;
    n->as.for_of.label = NULL;
    return n;
}

PSAstNode *ps_ast_switch(PSAstNode *expr, PSAstNode **cases, size_t case_count) {
    PSAstNode *n = alloc_node(AST_SWITCH);
    n->as.switch_stmt.expr = expr;
    n->as.switch_stmt.cases = cases;
    n->as.switch_stmt.case_count = case_count;
    n->as.switch_stmt.label = NULL;
    return n;
}

PSAstNode *ps_ast_case(PSAstNode *test, PSAstNode **items, size_t count) {
    PSAstNode *n = alloc_node(AST_CASE);
    n->as.case_stmt.test = test;
    n->as.case_stmt.items = items;
    n->as.case_stmt.count = count;
    return n;
}

PSAstNode *ps_ast_label(PSAstNode *label, PSAstNode *stmt) {
    PSAstNode *n = alloc_node(AST_LABEL);
    n->as.label_stmt.label = label;
    n->as.label_stmt.stmt = stmt;
    return n;
}

PSAstNode *ps_ast_break(PSAstNode *label) {
    PSAstNode *n = alloc_node(AST_BREAK);
    n->as.jump_stmt.label = label;
    return n;
}

PSAstNode *ps_ast_continue(PSAstNode *label) {
    PSAstNode *n = alloc_node(AST_CONTINUE);
    n->as.jump_stmt.label = label;
    return n;
}

PSAstNode *ps_ast_with(PSAstNode *object, PSAstNode *body) {
    PSAstNode *n = alloc_node(AST_WITH);
    n->as.with_stmt.object = object;
    n->as.with_stmt.body = body;
    return n;
}

PSAstNode *ps_ast_throw(PSAstNode *expr) {
    PSAstNode *n = alloc_node(AST_THROW);
    n->as.throw_stmt.expr = expr;
    return n;
}

PSAstNode *ps_ast_try(PSAstNode *try_block,
                      PSAstNode *catch_param,
                      PSAstNode *catch_block,
                      PSAstNode *finally_block) {
    PSAstNode *n = alloc_node(AST_TRY);
    n->as.try_stmt.try_block = try_block;
    n->as.try_stmt.catch_param = catch_param;
    n->as.try_stmt.catch_block = catch_block;
    n->as.try_stmt.finally_block = finally_block;
    return n;
}

PSAstNode *ps_ast_func_decl(PSAstNode *id,
                            PSAstNode **params,
                            PSAstNode **param_defaults,
                            size_t param_count,
                            PSAstNode *body) {
    PSAstNode *n = alloc_node(AST_FUNCTION_DECL);
    n->as.func_decl.id = id;
    n->as.func_decl.params = params;
    n->as.func_decl.param_defaults = param_defaults;
    n->as.func_decl.param_count = param_count;
    n->as.func_decl.body = body;
    return n;
}

PSAstNode *ps_ast_func_expr(PSAstNode *id,
                            PSAstNode **params,
                            PSAstNode **param_defaults,
                            size_t param_count,
                            PSAstNode *body) {
    PSAstNode *n = alloc_node(AST_FUNCTION_EXPR);
    n->as.func_expr.id = id;
    n->as.func_expr.params = params;
    n->as.func_expr.param_defaults = param_defaults;
    n->as.func_expr.param_count = param_count;
    n->as.func_expr.body = body;
    return n;
}

PSAstNode *ps_ast_identifier(const char *name, size_t length) {
    PSAstNode *n = alloc_node(AST_IDENTIFIER);
    char *copy = (char *)malloc(length + 1);
    if (!copy) {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }
    memcpy(copy, name, length);
    copy[length] = '\0';
    n->as.identifier.name = copy;
    n->as.identifier.length = length;
    return n;
}

PSAstNode *ps_ast_literal(PSValue value) {
    PSAstNode *n = alloc_node(AST_LITERAL);
    n->as.literal.value = value;
    return n;
}

PSAstNode *ps_ast_assign(int op, PSAstNode *target, PSAstNode *value) {
    PSAstNode *n = alloc_node(AST_ASSIGN);
    n->as.assign.op = op;
    n->as.assign.target = target;
    n->as.assign.value = value;
    return n;
}

PSAstNode *ps_ast_unary(int op, PSAstNode *expr) {
    PSAstNode *n = alloc_node(AST_UNARY);
    n->as.unary.op = op;
    n->as.unary.expr = expr;
    return n;
}

PSAstNode *ps_ast_update(int op, int is_prefix, PSAstNode *expr) {
    PSAstNode *n = alloc_node(AST_UPDATE);
    n->as.update.op = op;
    n->as.update.is_prefix = is_prefix;
    n->as.update.expr = expr;
    return n;
}

PSAstNode *ps_ast_conditional(PSAstNode *cond, PSAstNode *then_expr, PSAstNode *else_expr) {
    PSAstNode *n = alloc_node(AST_CONDITIONAL);
    n->as.conditional.cond = cond;
    n->as.conditional.then_expr = then_expr;
    n->as.conditional.else_expr = else_expr;
    return n;
}

PSAstNode *ps_ast_binary(int op, PSAstNode *left, PSAstNode *right) {
    PSAstNode *n = alloc_node(AST_BINARY);
    n->as.binary.op = op;
    n->as.binary.left = left;
    n->as.binary.right = right;
    return n;
}

PSAstNode *ps_ast_call(PSAstNode *callee, PSAstNode **args, size_t argc) {
    PSAstNode *n = alloc_node(AST_CALL);
    n->as.call.callee = callee;
    n->as.call.args = args;
    n->as.call.argc = argc;
    return n;
}

PSAstNode *ps_ast_member(PSAstNode *object, PSAstNode *property, int computed) {
    PSAstNode *n = alloc_node(AST_MEMBER);
    n->as.member.object = object;
    n->as.member.property = property;
    n->as.member.computed = computed;
    return n;
}

PSAstNode *ps_ast_new(PSAstNode *callee, PSAstNode **args, size_t argc) {
    PSAstNode *n = alloc_node(AST_NEW);
    n->as.new_expr.callee = callee;
    n->as.new_expr.args = args;
    n->as.new_expr.argc = argc;
    return n;
}

PSAstNode *ps_ast_array_literal(PSAstNode **items, size_t count) {
    PSAstNode *n = alloc_node(AST_ARRAY_LITERAL);
    n->as.array_literal.items = items;
    n->as.array_literal.count = count;
    return n;
}

PSAstNode *ps_ast_object_literal(PSAstProperty *props, size_t count) {
    PSAstNode *n = alloc_node(AST_OBJECT_LITERAL);
    n->as.object_literal.props = props;
    n->as.object_literal.count = count;
    return n;
}

/* --------------------------------------------------------- */
/* Destruction                                               */
/* --------------------------------------------------------- */

void ps_ast_free(PSAstNode *node) {
    if (!node) return;

    switch (node->kind) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (size_t i = 0; i < node->as.list.count; i++) {
                ps_ast_free(node->as.list.items[i]);
            }
            free(node->as.list.items);
            break;

        case AST_VAR_DECL:
            ps_ast_free(node->as.var_decl.id);
            ps_ast_free(node->as.var_decl.init);
            break;

        case AST_EXPR_STMT:
            ps_ast_free(node->as.expr_stmt.expr);
            break;

        case AST_RETURN:
            ps_ast_free(node->as.ret.expr);
            break;

        case AST_IF:
            ps_ast_free(node->as.if_stmt.cond);
            ps_ast_free(node->as.if_stmt.then_branch);
            ps_ast_free(node->as.if_stmt.else_branch);
            break;

        case AST_WHILE:
            ps_ast_free(node->as.while_stmt.cond);
            ps_ast_free(node->as.while_stmt.body);
            break;

        case AST_DO_WHILE:
            ps_ast_free(node->as.do_while.body);
            ps_ast_free(node->as.do_while.cond);
            break;

        case AST_FOR:
            ps_ast_free(node->as.for_stmt.init);
            ps_ast_free(node->as.for_stmt.test);
            ps_ast_free(node->as.for_stmt.update);
            ps_ast_free(node->as.for_stmt.body);
            break;

        case AST_FOR_IN:
            ps_ast_free(node->as.for_in.target);
            ps_ast_free(node->as.for_in.object);
            ps_ast_free(node->as.for_in.body);
            break;
        case AST_FOR_OF:
            ps_ast_free(node->as.for_of.target);
            ps_ast_free(node->as.for_of.object);
            ps_ast_free(node->as.for_of.body);
            break;

        case AST_SWITCH:
            ps_ast_free(node->as.switch_stmt.expr);
            for (size_t i = 0; i < node->as.switch_stmt.case_count; i++) {
                ps_ast_free(node->as.switch_stmt.cases[i]);
            }
            free(node->as.switch_stmt.cases);
            break;

        case AST_CASE:
            ps_ast_free(node->as.case_stmt.test);
            for (size_t i = 0; i < node->as.case_stmt.count; i++) {
                ps_ast_free(node->as.case_stmt.items[i]);
            }
            free(node->as.case_stmt.items);
            break;

        case AST_LABEL:
            ps_ast_free(node->as.label_stmt.label);
            ps_ast_free(node->as.label_stmt.stmt);
            break;

        case AST_BREAK:
        case AST_CONTINUE:
            ps_ast_free(node->as.jump_stmt.label);
            break;

        case AST_WITH:
            ps_ast_free(node->as.with_stmt.object);
            ps_ast_free(node->as.with_stmt.body);
            break;

        case AST_THROW:
            ps_ast_free(node->as.throw_stmt.expr);
            break;

        case AST_TRY:
            ps_ast_free(node->as.try_stmt.try_block);
            ps_ast_free(node->as.try_stmt.catch_param);
            ps_ast_free(node->as.try_stmt.catch_block);
            ps_ast_free(node->as.try_stmt.finally_block);
            break;

        case AST_FUNCTION_DECL:
            ps_ast_free(node->as.func_decl.id);
            for (size_t i = 0; i < node->as.func_decl.param_count; i++) {
                ps_ast_free(node->as.func_decl.params[i]);
                if (node->as.func_decl.param_defaults) {
                    ps_ast_free(node->as.func_decl.param_defaults[i]);
                }
            }
            free(node->as.func_decl.params);
            free(node->as.func_decl.param_defaults);
            ps_ast_free(node->as.func_decl.body);
            break;
        case AST_FUNCTION_EXPR:
            ps_ast_free(node->as.func_expr.id);
            for (size_t i = 0; i < node->as.func_expr.param_count; i++) {
                ps_ast_free(node->as.func_expr.params[i]);
                if (node->as.func_expr.param_defaults) {
                    ps_ast_free(node->as.func_expr.param_defaults[i]);
                }
            }
            free(node->as.func_expr.params);
            free(node->as.func_expr.param_defaults);
            ps_ast_free(node->as.func_expr.body);
            break;

        case AST_BINARY:
            ps_ast_free(node->as.binary.left);
            ps_ast_free(node->as.binary.right);
            break;

        case AST_ASSIGN:
            ps_ast_free(node->as.assign.target);
            ps_ast_free(node->as.assign.value);
            break;

        case AST_UNARY:
            ps_ast_free(node->as.unary.expr);
            break;

        case AST_UPDATE:
            ps_ast_free(node->as.update.expr);
            break;

        case AST_CONDITIONAL:
            ps_ast_free(node->as.conditional.cond);
            ps_ast_free(node->as.conditional.then_expr);
            ps_ast_free(node->as.conditional.else_expr);
            break;

        case AST_CALL:
            ps_ast_free(node->as.call.callee);
            for (size_t i = 0; i < node->as.call.argc; i++) {
                ps_ast_free(node->as.call.args[i]);
            }
            free(node->as.call.args);
            break;

        case AST_MEMBER:
            ps_ast_free(node->as.member.object);
            ps_ast_free(node->as.member.property);
            break;

        case AST_NEW:
            ps_ast_free(node->as.new_expr.callee);
            for (size_t i = 0; i < node->as.new_expr.argc; i++) {
                ps_ast_free(node->as.new_expr.args[i]);
            }
            free(node->as.new_expr.args);
            break;

        case AST_ARRAY_LITERAL:
            for (size_t i = 0; i < node->as.array_literal.count; i++) {
                if (node->as.array_literal.items[i]) {
                    ps_ast_free(node->as.array_literal.items[i]);
                }
            }
            free(node->as.array_literal.items);
            break;

        case AST_OBJECT_LITERAL:
            for (size_t i = 0; i < node->as.object_literal.count; i++) {
                ps_ast_free(node->as.object_literal.props[i].value);
            }
            free(node->as.object_literal.props);
            break;

        case AST_IDENTIFIER:
            free((void *)node->as.identifier.name);
            break;
        case AST_LITERAL:
            /* nothing to free */
            break;
    }

    free(node);
}
