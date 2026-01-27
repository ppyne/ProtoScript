#include "ps_function.h"
#include "ps_env.h"
#include "ps_ast.h"
#include "ps_string.h"
#include "ps_gc.h"
#include "ps_config.h"
#include "ps_vm.h"

#include <stdlib.h>
#include <string.h>

#if !PS_DISABLE_SPECIALIZATION
static PSString *ps_function_ident_string(PSAstNode *node) {
    if (!node || node->kind != AST_IDENTIFIER) return NULL;
    if (node->as.identifier.str) {
        return node->as.identifier.str;
    }
    return ps_string_from_utf8(node->as.identifier.name, node->as.identifier.length);
}

static int ps_function_string_equals(const PSString *a, const PSString *b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->byte_len != b->byte_len) return 0;
    if (!a->utf8 || !b->utf8) return 0;
    return memcmp(a->utf8, b->utf8, a->byte_len) == 0;
}

static int ps_slot_names_contains(PSString **names, size_t count, PSString *name) {
    if (!name) return 0;
    for (size_t i = 0; i < count; i++) {
        if (names[i] && ps_function_string_equals(names[i], name)) {
            return 1;
        }
    }
    return 0;
}

static int ps_slot_names_push(PSString ***names, size_t *count, size_t *cap, PSString *name) {
    if (!names || !count || !cap || !name) return 0;
    if (ps_slot_names_contains(*names, *count, name)) return 1;
    if (*count == *cap) {
        size_t new_cap = *cap ? (*cap * 2) : 8;
        PSString **next = (PSString **)realloc(*names, sizeof(PSString *) * new_cap);
        if (!next) return 0;
        *names = next;
        *cap = new_cap;
    }
    (*names)[(*count)++] = name;
    return 1;
}

static int ps_collect_slot_names(PSAstNode *node, PSString ***names, size_t *count, size_t *cap) {
    if (!node) return 1;
    switch (node->kind) {
        case AST_VAR_DECL: {
            PSString *name = ps_function_ident_string(node->as.var_decl.id);
            return ps_slot_names_push(names, count, cap, name);
        }
        case AST_PROGRAM:
        case AST_BLOCK: {
            for (size_t i = 0; i < node->as.list.count; i++) {
                if (!ps_collect_slot_names(node->as.list.items[i], names, count, cap)) {
                    return 0;
                }
            }
            return 1;
        }
        case AST_IF:
            return ps_collect_slot_names(node->as.if_stmt.then_branch, names, count, cap) &&
                   ps_collect_slot_names(node->as.if_stmt.else_branch, names, count, cap);
        case AST_WHILE:
            return ps_collect_slot_names(node->as.while_stmt.body, names, count, cap);
        case AST_DO_WHILE:
            return ps_collect_slot_names(node->as.do_while.body, names, count, cap);
        case AST_FOR:
            return ps_collect_slot_names(node->as.for_stmt.init, names, count, cap) &&
                   ps_collect_slot_names(node->as.for_stmt.body, names, count, cap);
        case AST_FOR_IN:
            if (node->as.for_in.is_var &&
                node->as.for_in.target &&
                node->as.for_in.target->kind == AST_IDENTIFIER) {
                PSString *name = ps_function_ident_string(node->as.for_in.target);
                if (!ps_slot_names_push(names, count, cap, name)) return 0;
            }
            return ps_collect_slot_names(node->as.for_in.body, names, count, cap);
        case AST_FOR_OF:
            if (node->as.for_of.is_var &&
                node->as.for_of.target &&
                node->as.for_of.target->kind == AST_IDENTIFIER) {
                PSString *name = ps_function_ident_string(node->as.for_of.target);
                if (!ps_slot_names_push(names, count, cap, name)) return 0;
            }
            return ps_collect_slot_names(node->as.for_of.body, names, count, cap);
        case AST_SWITCH:
            for (size_t i = 0; i < node->as.switch_stmt.case_count; i++) {
                if (!ps_collect_slot_names(node->as.switch_stmt.cases[i], names, count, cap)) {
                    return 0;
                }
            }
            return 1;
        case AST_CASE:
            for (size_t i = 0; i < node->as.case_stmt.count; i++) {
                if (!ps_collect_slot_names(node->as.case_stmt.items[i], names, count, cap)) {
                    return 0;
                }
            }
            return 1;
        case AST_WITH:
            return ps_collect_slot_names(node->as.with_stmt.body, names, count, cap);
        case AST_TRY:
            return ps_collect_slot_names(node->as.try_stmt.try_block, names, count, cap) &&
                   ps_collect_slot_names(node->as.try_stmt.catch_block, names, count, cap) &&
                   ps_collect_slot_names(node->as.try_stmt.finally_block, names, count, cap);
        case AST_LABEL:
            return ps_collect_slot_names(node->as.label_stmt.stmt, names, count, cap);
        case AST_FUNCTION_EXPR:
            return 1;
        default:
            return 1;
    }
}

static void ps_function_build_slot_map(PSFunction *func) {
    if (!func) return;
    PSString **names = NULL;
    size_t count = 0;
    size_t cap = 0;
    for (size_t i = 0; i < func->param_count; i++) {
        PSString *name = func->param_names ? func->param_names[i] : NULL;
        if (!name && func->params && func->params[i]) {
            name = ps_function_ident_string(func->params[i]);
        }
        if (!ps_slot_names_push(&names, &count, &cap, name)) {
            free(names);
            return;
        }
    }
    if (func->body) {
        if (!ps_collect_slot_names(func->body, &names, &count, &cap)) {
            free(names);
            return;
        }
    }
    func->slot_names = names;
    func->slot_count = count;
}
#endif

PSObject *ps_function_new_native(PSNativeFunc fn) {
    PSObject *obj = ps_object_new(NULL);
    if (!obj) return NULL;

    PSFunction *func = (PSFunction *)ps_gc_alloc(PS_GC_FUNCTION, sizeof(PSFunction));
    if (!func) {
        ps_object_free(obj);
        return NULL;
    }
#if PS_ENABLE_PERF
    {
        PSVM *vm = ps_gc_active_vm();
        if (vm) {
            vm->perf.function_new++;
        }
    }
#endif
    func->is_native = 1;
    func->native = fn;
    func->param_names = NULL;
    func->name = NULL;
    func->param_count = 0;
    func->fast_names = NULL;
    func->fast_count = 0;
    func->fast_param_index = NULL;
    func->fast_local_count = 0;
    func->fast_this_index = 0;
    func->fast_local_index = NULL;
    func->fast_local_index_count = 0;
    func->fast_math_expr = NULL;
    func->fast_num_inits = NULL;
    func->fast_num_names = NULL;
    func->fast_num_count = 0;
    func->fast_num_return = NULL;
    func->fast_num_if_cond = NULL;
    func->fast_num_if_return = NULL;
    func->fast_num_ops = NULL;
    func->fast_num_ops_count = 0;
    func->fast_clamp_min = 0.0;
    func->fast_clamp_max = 0.0;
    func->fast_clamp_use_floor = 0;
    func->fast_env = NULL;
    func->fast_env_in_use = 0;
    func->stmt_bc = NULL;
    func->stmt_bc_state = 0;
    func->fast_flags = 0;
    func->fast_checked = 0;
#if !PS_DISABLE_SPECIALIZATION
    func->slot_names = NULL;
    func->slot_count = 0;
    func->spec_slot_names = NULL;
    func->spec_slot_count = 0;
    func->spec_hot_count = 0;
    func->spec_bc = NULL;
    func->spec_bc_state = 0;
    func->spec_guard_count = 0;
    for (size_t i = 0; i < PS_SPECIALIZATION_GUARD_MAX; i++) {
        func->spec_guard_slots[i] = 0;
    }
#endif
#if !PS_DISABLE_UNBOXED_SPEC
    func->unboxed_hot_count = 0;
    func->unboxed_bc = NULL;
    func->unboxed_bc_state = 0;
    func->unboxed_guard_count = 0;
    func->unboxed_used_count = 0;
    for (size_t i = 0; i < PS_SPECIALIZATION_GUARD_MAX; i++) {
        func->unboxed_guard_slots[i] = 0;
        func->unboxed_used_slots[i] = 0;
    }
    for (size_t i = 0; i < (PS_SPECIALIZATION_SLOT_MAX + 31) / 32; i++) {
        func->unboxed_write_bits[i] = 0;
    }
#endif

    obj->kind = PS_OBJ_KIND_FUNCTION;
    obj->internal = func;
    return obj;
}

PSObject *ps_function_new_script(PSAstNode **params,
                                 PSAstNode **param_defaults,
                                 size_t param_count,
                                 PSAstNode *body,
                                 PSEnv *env) {
    PSObject *obj = ps_object_new(NULL);
    if (!obj) return NULL;

    PSFunction *func = (PSFunction *)ps_gc_alloc(PS_GC_FUNCTION, sizeof(PSFunction));
    if (!func) {
        ps_object_free(obj);
        return NULL;
    }
#if PS_ENABLE_PERF
    {
        PSVM *vm = ps_gc_active_vm();
        if (vm) {
            vm->perf.function_new++;
        }
    }
#endif

    func->is_native = 0;
    func->params = params;
    func->param_defaults = param_defaults;
    func->param_count = param_count;
    func->body = body;
    func->env = env;
    func->param_names = NULL;
    func->name = NULL;
    func->fast_flags = 0;
    func->fast_checked = 0;
    func->fast_names = NULL;
    func->fast_count = 0;
    func->fast_param_index = NULL;
    func->fast_local_count = 0;
    func->fast_this_index = 0;
    func->fast_local_index = NULL;
    func->fast_local_index_count = 0;
    func->fast_math_expr = NULL;
    func->fast_num_inits = NULL;
    func->fast_num_names = NULL;
    func->fast_num_count = 0;
    func->fast_num_return = NULL;
    func->fast_num_if_cond = NULL;
    func->fast_num_if_return = NULL;
    func->fast_num_ops = NULL;
    func->fast_num_ops_count = 0;
    func->fast_clamp_min = 0.0;
    func->fast_clamp_max = 0.0;
    func->fast_clamp_use_floor = 0;
    func->fast_env = NULL;
    func->fast_env_in_use = 0;
    func->stmt_bc = NULL;
    func->stmt_bc_state = 0;
#if !PS_DISABLE_SPECIALIZATION
    func->slot_names = NULL;
    func->slot_count = 0;
    func->spec_slot_names = NULL;
    func->spec_slot_count = 0;
    func->spec_hot_count = 0;
    func->spec_bc = NULL;
    func->spec_bc_state = 0;
    func->spec_guard_count = 0;
    for (size_t i = 0; i < PS_SPECIALIZATION_GUARD_MAX; i++) {
        func->spec_guard_slots[i] = 0;
    }
#endif
#if !PS_DISABLE_UNBOXED_SPEC
    func->unboxed_hot_count = 0;
    func->unboxed_bc = NULL;
    func->unboxed_bc_state = 0;
    func->unboxed_guard_count = 0;
    func->unboxed_used_count = 0;
    for (size_t i = 0; i < PS_SPECIALIZATION_GUARD_MAX; i++) {
        func->unboxed_guard_slots[i] = 0;
        func->unboxed_used_slots[i] = 0;
    }
    for (size_t i = 0; i < (PS_SPECIALIZATION_SLOT_MAX + 31) / 32; i++) {
        func->unboxed_write_bits[i] = 0;
    }
#endif
    if (param_count > 0 && params) {
        func->param_names = (PSString **)calloc(param_count, sizeof(PSString *));
        if (func->param_names) {
            for (size_t i = 0; i < param_count; i++) {
                if (params[i] && params[i]->kind == AST_IDENTIFIER) {
                    PSAstNode *param = params[i];
                    if (param->as.identifier.str) {
                        func->param_names[i] = param->as.identifier.str;
                    } else {
                        func->param_names[i] = ps_string_from_utf8(
                            param->as.identifier.name,
                            param->as.identifier.length
                        );
                    }
                }
            }
        }
    }
#if !PS_DISABLE_SPECIALIZATION
    ps_function_build_slot_map(func);
#endif

    obj->kind = PS_OBJ_KIND_FUNCTION;
    obj->internal = func;
    return obj;
}

PSFunction *ps_function_from_object(PSObject *obj) {
    if (!obj || obj->kind != PS_OBJ_KIND_FUNCTION) {
        return NULL;
    }
    return (PSFunction *)obj->internal;
}

void ps_function_setup(PSObject *fn_obj,
                       PSObject *function_proto,
                       PSObject *object_proto,
                       PSObject *prototype_override) {
    if (!fn_obj) return;

    if (function_proto) {
        fn_obj->prototype = function_proto;
    }

    PSObject *proto_obj = prototype_override;
    if (!proto_obj && object_proto) {
        proto_obj = ps_object_new(object_proto);
        if (proto_obj) {
            ps_object_define(proto_obj,
                             ps_string_from_cstr("constructor"),
                             ps_value_object(fn_obj),
                             PS_ATTR_DONTENUM);
        }
    }

    if (proto_obj) {
        ps_object_define(fn_obj,
                         ps_string_from_cstr("prototype"),
                         ps_value_object(proto_obj),
                         PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
    }
}
