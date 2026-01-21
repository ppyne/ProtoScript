#include "ps_ast.h"
#include "ps_vm.h"
#include "ps_eval.h"
#include "ps_object.h"
#include "ps_value.h"
#include "ps_string.h"
#include "ps_lexer.h"
#include "ps_parser.h"
#include "ps_env.h"
#include "ps_function.h"
#include "ps_config.h"
#include "ps_gc.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdint.h>

/* --------------------------------------------------------- */
/* Forward declarations                                      */
/* --------------------------------------------------------- */

typedef struct {
    int did_return;
    int did_break;
    int did_continue;
    int did_throw;
    PSValue throw_value;
    PSString *break_label;
    PSString *continue_label;
} PSEvalControl;

static PSValue eval_node(PSVM *vm, PSEnv *env, PSAstNode *node, PSEvalControl *ctl);
static PSValue eval_expression(PSVM *vm, PSEnv *env, PSAstNode *node, PSEvalControl *ctl);
static PSValue eval_block(PSVM *vm, PSEnv *env, PSAstNode *node, PSEvalControl *ctl);
static PSValue ps_eval_source(PSVM *vm, PSEnv *env, PSString *source, PSEvalControl *ctl);
static void hoist_decls(PSVM *vm, PSEnv *env, PSAstNode *node);

static int ps_value_is_nan(double v) {
    return isnan(v);
}

static int ps_call_method(PSVM *vm, PSObject *obj, const char *name, PSValue *out) {
    int found = 0;
    PSValue method = ps_object_get(obj, ps_string_from_cstr(name), &found);
    if (!found || method.type != PS_T_OBJECT || !method.as.object ||
        method.as.object->kind != PS_OBJ_KIND_FUNCTION) {
        return 0;
    }
    int did_throw = 0;
    PSValue throw_value = ps_value_undefined();
    PSValue result = ps_eval_call_function(vm,
                                           vm ? vm->env : NULL,
                                           method.as.object,
                                           ps_value_object(obj),
                                           0,
                                           NULL,
                                           &did_throw,
                                           &throw_value);
    if (did_throw) {
        if (vm) {
            vm->pending_throw = throw_value;
            vm->has_pending_throw = 1;
        }
        return -1;
    }
    if (out) *out = result;
    return 1;
}

static PSValue ps_eval_source(PSVM *vm, PSEnv *env, PSString *source, PSEvalControl *ctl) {
    if (!source) {
        return ps_value_undefined();
    }

    char *buf = malloc(source->byte_len + 1);
    if (!buf) {
        return ps_value_undefined();
    }
    memcpy(buf, source->utf8, source->byte_len);
    buf[source->byte_len] = '\0';

    PSAstNode *program = ps_parse(buf);
    free(buf);
    if (!program) {
        ctl->did_throw = 1;
        ctl->throw_value = ps_vm_make_error(vm, "SyntaxError", "eval parse failed");
        return ctl->throw_value;
    }

    PSAstNode *prev_ast = vm ? vm->current_ast : NULL;
    if (vm) vm->current_ast = program;

    PSEvalControl inner = {0};
    PSValue last = ps_value_undefined();
    hoist_decls(vm, env, program);
    for (size_t i = 0; i < program->as.list.count; i++) {
        last = eval_node(vm, env, program->as.list.items[i], &inner);
        if (inner.did_throw || inner.did_return || inner.did_break || inner.did_continue) {
            break;
        }
    }

    if (inner.did_throw) {
        ctl->did_throw = 1;
        ctl->throw_value = inner.throw_value;
        ps_ast_free(program);
        if (vm) vm->current_ast = prev_ast;
        return ctl->throw_value;
    }

    ps_ast_free(program);
    if (vm) vm->current_ast = prev_ast;
    return last;
}

static int ps_check_pending_throw(PSVM *vm, PSEvalControl *ctl) {
    if (vm && vm->has_pending_throw) {
        ctl->did_throw = 1;
        ctl->throw_value = vm->pending_throw;
        vm->has_pending_throw = 0;
        return 1;
    }
    return 0;
}

static void ps_define_script_function_props(PSObject *fn, PSString *name, size_t param_count) {
    if (!fn) return;
    ps_object_define(fn,
                     ps_string_from_cstr("length"),
                     ps_value_number((double)param_count),
                     PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
    if (name) {
        ps_object_define(fn,
                         ps_string_from_cstr("name"),
                         ps_value_string(name),
                         PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
    }
}

PSValue ps_to_primitive(PSVM *vm, PSValue value, PSToPrimitiveHint hint) {
    if (value.type != PS_T_OBJECT || !value.as.object) {
        return value;
    }
    PSObject *obj = value.as.object;
    PSToPrimitiveHint use_hint = hint;
    if (use_hint == PS_HINT_NONE) {
        use_hint = (obj->kind == PS_OBJ_KIND_DATE) ? PS_HINT_STRING : PS_HINT_NUMBER;
    }

    const char *first = (use_hint == PS_HINT_STRING) ? "toString" : "valueOf";
    const char *second = (use_hint == PS_HINT_STRING) ? "valueOf" : "toString";
    PSValue result = ps_value_undefined();
    int rc = ps_call_method(vm, obj, first, &result);
    if (rc < 0) return ps_value_undefined();
    if (rc > 0 && ps_value_is_primitive(&result)) return result;
    rc = ps_call_method(vm, obj, second, &result);
    if (rc < 0) return ps_value_undefined();
    if (rc > 0 && ps_value_is_primitive(&result)) return result;

    if (vm) {
        vm->pending_throw = ps_vm_make_error(vm, "TypeError", "Cannot convert object to primitive");
        vm->has_pending_throw = 1;
    }
    return ps_value_undefined();
}

PSString *ps_to_string(PSVM *vm, PSValue value) {
    PSValue prim = ps_to_primitive(vm, value, PS_HINT_STRING);
    if (vm && vm->has_pending_throw) return ps_string_from_cstr("");
    return ps_value_to_string(&prim);
}

double ps_to_number(PSVM *vm, PSValue value) {
    PSValue prim = ps_to_primitive(vm, value, PS_HINT_NUMBER);
    if (vm && vm->has_pending_throw) return 0.0 / 0.0;
    switch (prim.type) {
        case PS_T_UNDEFINED:
            return 0.0 / 0.0;
        case PS_T_NULL:
            return 0.0;
        case PS_T_BOOLEAN:
            return prim.as.boolean ? 1.0 : 0.0;
        case PS_T_NUMBER:
            return prim.as.number;
        case PS_T_STRING:
            return ps_string_to_number(prim.as.string);
        default:
            return 0.0;
    }
}

int ps_to_boolean(PSVM *vm, PSValue value) {
    (void)vm;
    switch (value.type) {
        case PS_T_UNDEFINED:
        case PS_T_NULL:
            return 0;
        case PS_T_BOOLEAN:
            return value.as.boolean ? 1 : 0;
        case PS_T_NUMBER:
            if (ps_value_is_nan(value.as.number) || value.as.number == 0.0) return 0;
            return 1;
        case PS_T_STRING:
            return value.as.string && value.as.string->glyph_count > 0;
        case PS_T_OBJECT:
            return 1;
        default:
            return 0;
    }
}

static int ps_string_equals(const PSString *a, const PSString *b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->glyph_count != b->glyph_count) return 0;
    for (size_t i = 0; i < a->glyph_count; i++) {
        if (ps_string_char_code_at(a, i) != ps_string_char_code_at(b, i)) {
            return 0;
        }
    }
    return 1;
}

static int ps_string_compare(const PSString *a, const PSString *b) {
    size_t min = (a->glyph_count < b->glyph_count) ? a->glyph_count : b->glyph_count;
    for (size_t i = 0; i < min; i++) {
        uint32_t ca = ps_string_char_code_at(a, i);
        uint32_t cb = ps_string_char_code_at(b, i);
        if (ca < cb) return -1;
        if (ca > cb) return 1;
    }
    if (a->glyph_count < b->glyph_count) return -1;
    if (a->glyph_count > b->glyph_count) return 1;
    return 0;
}

static int32_t ps_to_int32(PSVM *vm, const PSValue *v) {
    double num = ps_to_number(vm, *v);
    if (ps_value_is_nan(num) || num == 0.0 || isinf(num)) return 0;
    double sign = num < 0 ? -1.0 : 1.0;
    double abs = floor(fabs(num));
    double val = fmod(sign * abs, 4294967296.0);
    if (val >= 2147483648.0) val -= 4294967296.0;
    return (int32_t)val;
}

static uint32_t ps_to_uint32(PSVM *vm, const PSValue *v) {
    double num = ps_to_number(vm, *v);
    if (ps_value_is_nan(num) || num == 0.0 || isinf(num)) return 0;
    double sign = num < 0 ? -1.0 : 1.0;
    double abs = floor(fabs(num));
    double val = fmod(sign * abs, 4294967296.0);
    if (val < 0) val += 4294967296.0;
    return (uint32_t)val;
}

static int ps_strict_equals(const PSValue *a, const PSValue *b) {
    if (a->type != b->type) return 0;
    switch (a->type) {
        case PS_T_UNDEFINED:
        case PS_T_NULL:
            return 1;
        case PS_T_BOOLEAN:
            return a->as.boolean == b->as.boolean;
        case PS_T_NUMBER:
            if (ps_value_is_nan(a->as.number) || ps_value_is_nan(b->as.number)) return 0;
            return a->as.number == b->as.number;
        case PS_T_STRING:
            return ps_string_equals(a->as.string, b->as.string);
        case PS_T_OBJECT:
            return a->as.object == b->as.object;
        default:
            return 0;
    }
}

static int ps_abstract_equals(PSVM *vm, const PSValue *a, const PSValue *b, PSEvalControl *ctl) {
    if (a->type == b->type) {
        return ps_strict_equals(a, b);
    }

    if ((a->type == PS_T_NULL && b->type == PS_T_UNDEFINED) ||
        (a->type == PS_T_UNDEFINED && b->type == PS_T_NULL)) {
        return 1;
    }

    if (a->type == PS_T_NUMBER && b->type == PS_T_STRING) {
        double bn = ps_to_number(vm, *b);
        if (ps_check_pending_throw(vm, ctl)) return 0;
        if (ps_value_is_nan(a->as.number) || ps_value_is_nan(bn)) return 0;
        return a->as.number == bn;
    }
    if (a->type == PS_T_STRING && b->type == PS_T_NUMBER) {
        double an = ps_to_number(vm, *a);
        if (ps_check_pending_throw(vm, ctl)) return 0;
        if (ps_value_is_nan(an) || ps_value_is_nan(b->as.number)) return 0;
        return an == b->as.number;
    }

    if (a->type == PS_T_BOOLEAN) {
        PSValue na = ps_value_number(a->as.boolean ? 1.0 : 0.0);
        return ps_abstract_equals(vm, &na, b, ctl);
    }
    if (b->type == PS_T_BOOLEAN) {
        PSValue nb = ps_value_number(b->as.boolean ? 1.0 : 0.0);
        return ps_abstract_equals(vm, a, &nb, ctl);
    }

    if (a->type == PS_T_OBJECT &&
        (b->type == PS_T_STRING || b->type == PS_T_NUMBER)) {
        PSValue prim = ps_to_primitive(vm, *a, PS_HINT_NONE);
        if (ps_check_pending_throw(vm, ctl)) return 0;
        return ps_abstract_equals(vm, &prim, b, ctl);
    }
    if (b->type == PS_T_OBJECT &&
        (a->type == PS_T_STRING || a->type == PS_T_NUMBER)) {
        PSValue prim = ps_to_primitive(vm, *b, PS_HINT_NONE);
        if (ps_check_pending_throw(vm, ctl)) return 0;
        return ps_abstract_equals(vm, a, &prim, ctl);
    }

    return 0;
}

static PSObject *ps_to_object(PSVM *vm, const PSValue *v, PSEvalControl *ctl) {
    if (v->type == PS_T_OBJECT) return v->as.object;
    if (v->type == PS_T_NULL || v->type == PS_T_UNDEFINED) {
        ctl->did_throw = 1;
        ctl->throw_value = ps_vm_make_error(vm, "TypeError", "Cannot convert null/undefined to object");
        return NULL;
    }
    PSObject *obj = ps_vm_wrap_primitive(vm, v);
    if (!obj) {
        obj = ps_object_new(vm ? vm->object_proto : NULL);
        if (!obj) return NULL;
    }
    return obj;
}

static PSString *ps_member_key(PSVM *vm, PSEnv *env, PSAstNode *member, PSEvalControl *ctl) {
    if (!member || member->kind != AST_MEMBER) return NULL;
    if (!member->as.member.computed) {
        return ps_string_from_utf8(
            member->as.member.property->as.identifier.name,
            member->as.member.property->as.identifier.length
        );
    }
    PSValue key_val = eval_expression(vm, env, member->as.member.property, ctl);
    if (ctl->did_throw) return NULL;
    PSString *key = ps_to_string(vm, key_val);
    if (ps_check_pending_throw(vm, ctl)) return NULL;
    return key;
}

static int ps_string_is_length(const PSString *s) {
    static const char *length_str = "length";
    if (!s || s->byte_len != 6) return 0;
    return memcmp(s->utf8, length_str, 6) == 0;
}

typedef struct {
    PSString *name;
    size_t index;
} PSIndexName;

static int ps_index_name_compare(const void *a, const void *b) {
    const PSIndexName *ia = (const PSIndexName *)a;
    const PSIndexName *ib = (const PSIndexName *)b;
    if (ia->index < ib->index) return -1;
    if (ia->index > ib->index) return 1;
    return 0;
}


static int ps_array_set_length(PSVM *vm, PSObject *obj, PSValue value, PSEvalControl *ctl) {
    if (!obj || obj->kind != PS_OBJ_KIND_ARRAY) return 0;
    double num = ps_to_number(vm, value);
    if (ps_check_pending_throw(vm, ctl)) return -1;
    if (isnan(num) || isinf(num) || num < 0.0 || floor(num) != num || num > 4294967295.0) {
        ctl->did_throw = 1;
        ctl->throw_value = ps_vm_make_error(vm, "RangeError", "Invalid array length");
        return -1;
    }

    size_t new_len = (size_t)num;
    size_t old_len = 0;
    int found = 0;
    PSValue len_val = ps_object_get(obj, ps_string_from_cstr("length"), &found);
    if (found) {
        double len_num = ps_value_to_number(&len_val);
        if (!isnan(len_num) && len_num > 0.0) {
            old_len = (size_t)len_num;
        }
    }
    if (new_len < old_len) {
        for (size_t i = new_len; i < old_len; i++) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%zu", i);
            int deleted = 0;
            ps_object_delete(obj, ps_string_from_cstr(buf), &deleted);
        }
    }
    ps_object_put(obj, ps_string_from_cstr("length"), ps_value_number((double)new_len));
    return 1;
}

static int ps_assign_for_target(PSVM *vm,
                                PSEnv *env,
                                PSAstNode *target,
                                int is_var,
                                PSValue value,
                                PSEvalControl *ctl) {
    if (!target) return 0;
    if (is_var) {
        if (target->kind == AST_IDENTIFIER) {
            PSString *name = ps_string_from_utf8(
                target->as.identifier.name,
                target->as.identifier.length
            );
            ps_env_define(env, name, value);
        }
        return 1;
    }
    if (target->kind == AST_IDENTIFIER) {
        PSString *name = ps_string_from_utf8(
            target->as.identifier.name,
            target->as.identifier.length
        );
        ps_env_set(env, name, value);
        return 1;
    }
    if (target->kind == AST_MEMBER) {
        PSValue target_val = eval_expression(vm, env, target->as.member.object, ctl);
        if (ctl->did_throw) return 0;
        PSObject *target_obj = ps_to_object(vm, &target_val, ctl);
        if (ctl->did_throw) return 0;
        PSString *prop = ps_member_key(vm, env, target, ctl);
        if (ctl->did_throw) return 0;
        ps_object_put(target_obj, prop, value);
        (void)ps_env_update_arguments(env, target_obj, prop, value);
        return 1;
    }
    return 0;
}

static int ps_string_to_array_index(const PSString *name, size_t *out_index) {
    if (!name || name->byte_len == 0 || !name->utf8) return 0;
    const unsigned char *p = (const unsigned char *)name->utf8;
    if (name->byte_len > 1 && p[0] == '0') return 0;
    unsigned long long value = 0;
    for (size_t i = 0; i < name->byte_len; i++) {
        if (p[i] < '0' || p[i] > '9') return 0;
        value = value * 10ULL + (unsigned long long)(p[i] - '0');
        if (value >= 4294967295ULL) return 0;
    }
    if (out_index) *out_index = (size_t)value;
    return 1;
}

static void ps_array_update_length(PSObject *obj, PSString *prop) {
    if (!obj || obj->kind != PS_OBJ_KIND_ARRAY || !prop) return;
    size_t index = 0;
    if (!ps_string_to_array_index(prop, &index)) return;
    size_t len = 0;
    int found = 0;
    PSValue len_val = ps_object_get(obj, ps_string_from_cstr("length"), &found);
    if (found) {
        double num = ps_value_to_number(&len_val);
        if (!isnan(num) && num >= 0.0) {
            len = (size_t)num;
        }
    }
    if (index + 1 > len) {
        ps_object_put(obj,
                      ps_string_from_cstr("length"),
                      ps_value_number((double)(index + 1)));
    }
}

static int ps_eval_args(PSVM *vm,
                        PSEnv *env,
                        PSAstNode **args,
                        size_t argc,
                        PSValue **out_args,
                        PSEvalControl *ctl) {
    (void)vm;
    if (argc == 0) {
        *out_args = NULL;
        return 1;
    }
    PSValue *vals = (PSValue *)calloc(argc, sizeof(PSValue));
    if (!vals) return 0;
    for (size_t i = 0; i < argc; i++) {
        vals[i] = eval_expression(vm, env, args[i], ctl);
        if (ctl->did_throw) {
            free(vals);
            return 0;
        }
    }
    *out_args = vals;
    return 1;
}

static int ps_collect_bound_args(PSObject *obj, PSValue **out_args, size_t *out_len) {
    *out_args = NULL;
    if (out_len) *out_len = 0;
    if (!obj) return 1;
    int found = 0;
    PSValue len_val = ps_object_get(obj, ps_string_from_cstr("length"), &found);
    if (!found) return 1;
    double num = ps_value_to_number(&len_val);
    if (isnan(num) || num < 0.0) return 1;
    size_t len = (size_t)num;
    if (out_len) *out_len = len;
    if (len == 0) return 1;
    PSValue *args = (PSValue *)calloc(len, sizeof(PSValue));
    if (!args) return 0;
    for (size_t i = 0; i < len; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%zu", i);
        int got = 0;
        PSValue val = ps_object_get(obj, ps_string_from_cstr(buf), &got);
        args[i] = got ? val : ps_value_undefined();
    }
    *out_args = args;
    return 1;
}
PSValue ps_eval_call_function(PSVM *vm,
                              PSEnv *env,
                              PSObject *fn_obj,
                              PSValue this_val,
                              int argc,
                              PSValue *argv,
                              int *did_throw,
                              PSValue *throw_value) {
    if (did_throw) *did_throw = 0;
    if (throw_value) *throw_value = ps_value_undefined();

    if (!fn_obj || fn_obj->kind != PS_OBJ_KIND_FUNCTION) {
        if (did_throw) *did_throw = 1;
        if (throw_value) *throw_value = ps_vm_make_error(vm, "TypeError", "Not a callable object");
        return throw_value ? *throw_value : ps_value_undefined();
    }

    PSFunction *func = ps_function_from_object(fn_obj);
    if (!func) {
        if (did_throw) *did_throw = 1;
        if (throw_value) *throw_value = ps_vm_make_error(vm, "TypeError", "Not a callable object");
        return throw_value ? *throw_value : ps_value_undefined();
    }

    if (this_val.type != PS_T_OBJECT &&
        this_val.type != PS_T_NULL &&
        this_val.type != PS_T_UNDEFINED) {
        PSObject *boxed = ps_vm_wrap_primitive(vm, &this_val);
        if (boxed) {
            this_val = ps_value_object(boxed);
        }
    }

    if (func->is_native) {
        PSObject *prev = vm ? vm->current_callee : NULL;
        if (vm) vm->current_callee = fn_obj;
        PSValue result = func->native(vm, this_val, argc, argv);
        if (vm) vm->current_callee = prev;
        if (vm && vm->has_pending_throw) {
            if (did_throw) *did_throw = 1;
            if (throw_value) *throw_value = vm->pending_throw;
            vm->has_pending_throw = 0;
            return throw_value ? *throw_value : ps_value_undefined();
        }
        return result;
    }

    PSEnv *call_env = ps_env_new_object(func->env ? func->env : env);
    if (!call_env) return ps_value_undefined();
    if (vm && vm->object_proto && call_env->record) {
        call_env->record->prototype = vm->object_proto;
    }

    ps_object_define(call_env->record,
                     ps_string_from_cstr("this"),
                     this_val,
                     PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);

    hoist_decls(vm, call_env, func->body);

    PSObject *args_obj = ps_object_new(vm ? vm->object_proto : NULL);
    if (args_obj) {
        for (int i = 0; i < argc; i++) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", i);
            ps_object_define(args_obj, ps_string_from_cstr(buf), argv[i], PS_ATTR_NONE);
        }
        ps_object_define(args_obj,
                         ps_string_from_cstr("length"),
                         ps_value_number((double)argc),
                         PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
        ps_object_define(args_obj,
                         ps_string_from_cstr("callee"),
                         ps_value_object(fn_obj),
                         PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE | PS_ATTR_READONLY);
        ps_object_define(call_env->record,
                         ps_string_from_cstr("arguments"),
                         ps_value_object(args_obj),
                         PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
#if PS_ENABLE_ARGUMENTS_ALIASING
        call_env->arguments_obj = args_obj;
#endif
    }

    for (size_t i = 0; i < func->param_count; i++) {
        PSAstNode *param = func->params[i];
        PSString *name = ps_string_from_utf8(
            param->as.identifier.name,
            param->as.identifier.length
        );
        PSValue val = ((int)i < argc) ? argv[i] : ps_value_undefined();
        ps_env_define(call_env, name, val);
    }

#if PS_ENABLE_ARGUMENTS_ALIASING
    if (func->param_count > 0) {
        call_env->param_names = calloc(func->param_count, sizeof(PSString *));
        if (call_env->param_names) {
            call_env->param_count = func->param_count;
            for (size_t i = 0; i < func->param_count; i++) {
                PSAstNode *param = func->params[i];
                call_env->param_names[i] = ps_string_from_utf8(
                    param->as.identifier.name,
                    param->as.identifier.length
                );
            }
        }
    }
#endif

    for (size_t i = 0; i < func->param_count; i++) {
        PSAstNode *default_expr = func->param_defaults ? func->param_defaults[i] : NULL;
        if (!default_expr) continue;
        PSValue current = ((int)i < argc) ? argv[i] : ps_value_undefined();
        if (current.type != PS_T_UNDEFINED) continue;
        PSEvalControl default_ctl = {0};
        PSValue default_val = eval_expression(vm, call_env, default_expr, &default_ctl);
        if (default_ctl.did_throw) {
            ps_env_free(call_env);
            if (did_throw) *did_throw = 1;
            if (throw_value) *throw_value = default_ctl.throw_value;
            return throw_value ? *throw_value : ps_value_undefined();
        }
        PSAstNode *param = func->params[i];
        PSString *name = ps_string_from_utf8(
            param->as.identifier.name,
            param->as.identifier.length
        );
        if ((int)i < argc) {
            ps_env_set(call_env, name, default_val);
        } else {
            ps_object_put(call_env->record, name, default_val);
        }
    }

    PSEvalControl inner = {0};
    PSEnv *prev_env = vm ? vm->env : NULL;
    if (vm) vm->env = call_env;
    PSValue ret = eval_node(vm, call_env, func->body, &inner);
    if (vm) vm->env = prev_env;
    ps_env_free(call_env);
    if (inner.did_throw) {
        if (did_throw) *did_throw = 1;
        if (throw_value) *throw_value = inner.throw_value;
        return throw_value ? *throw_value : ps_value_undefined();
    }
    return inner.did_return ? ret : ps_value_undefined();
}

typedef struct {
    PSString **items;
    size_t count;
    size_t cap;
} PSNameList;

static int ps_name_list_contains(PSNameList *list, PSString *name) {
    for (size_t i = 0; i < list->count; i++) {
        if (ps_string_equals(list->items[i], name)) {
            return 1;
        }
    }
    return 0;
}

static int ps_collect_enum_name(PSString *name, PSValue value, uint8_t attrs, void *user) {
    (void)value;
    (void)attrs;
    PSNameList *list = (PSNameList *)user;
    if (ps_name_list_contains(list, name)) return 0;
    if (list->count == list->cap) {
        size_t new_cap = list->cap ? list->cap * 2 : 8;
        PSString **next = (PSString **)realloc(list->items, sizeof(PSString *) * new_cap);
        if (!next) return 1;
        list->items = next;
        list->cap = new_cap;
    }
    list->items[list->count++] = name;
    return 0;
}

static void hoist_list(PSVM *vm, PSEnv *env, PSAstNode **items, size_t count) {
    for (size_t i = 0; i < count; i++) {
        hoist_decls(vm, env, items[i]);
    }
}

static void hoist_decls(PSVM *vm, PSEnv *env, PSAstNode *node) {
    if (!node) return;

    switch (node->kind) {
        case AST_VAR_DECL: {
            PSString *name = ps_string_from_utf8(
                node->as.var_decl.id->as.identifier.name,
                node->as.var_decl.id->as.identifier.length
            );
            int found = 0;
            (void)ps_object_get_own(env->record, name, &found);
            if (!found) {
                ps_env_define(env, name, ps_value_undefined());
            }
            break;
        }
        case AST_FUNCTION_DECL: {
            PSString *name = ps_string_from_utf8(
                node->as.func_decl.id->as.identifier.name,
                node->as.func_decl.id->as.identifier.length
            );
            PSObject *fn_obj = ps_function_new_script(
                node->as.func_decl.params,
                node->as.func_decl.param_defaults,
                node->as.func_decl.param_count,
                node->as.func_decl.body,
                env
            );
            if (fn_obj) {
                ps_function_setup(fn_obj, vm->function_proto, vm->object_proto, NULL);
                ps_define_script_function_props(fn_obj, name, node->as.func_decl.param_count);
                ps_env_define(env, name, ps_value_object(fn_obj));
            }
            break;
        }
        case AST_PROGRAM:
        case AST_BLOCK:
            hoist_list(vm, env, node->as.list.items, node->as.list.count);
            break;
        case AST_IF:
            hoist_decls(vm, env, node->as.if_stmt.then_branch);
            hoist_decls(vm, env, node->as.if_stmt.else_branch);
            break;
        case AST_WHILE:
            hoist_decls(vm, env, node->as.while_stmt.body);
            break;
        case AST_DO_WHILE:
            hoist_decls(vm, env, node->as.do_while.body);
            break;
        case AST_FOR:
            hoist_decls(vm, env, node->as.for_stmt.init);
            hoist_decls(vm, env, node->as.for_stmt.body);
            break;
        case AST_FOR_IN:
            if (node->as.for_in.is_var &&
                node->as.for_in.target &&
                node->as.for_in.target->kind == AST_IDENTIFIER) {
                PSString *name = ps_string_from_utf8(
                    node->as.for_in.target->as.identifier.name,
                    node->as.for_in.target->as.identifier.length
                );
                int found = 0;
                (void)ps_object_get_own(env->record, name, &found);
                if (!found) {
                    ps_env_define(env, name, ps_value_undefined());
                }
            }
            hoist_decls(vm, env, node->as.for_in.body);
            break;
        case AST_FOR_OF:
            if (node->as.for_of.is_var &&
                node->as.for_of.target &&
                node->as.for_of.target->kind == AST_IDENTIFIER) {
                PSString *name = ps_string_from_utf8(
                    node->as.for_of.target->as.identifier.name,
                    node->as.for_of.target->as.identifier.length
                );
                int found = 0;
                (void)ps_object_get_own(env->record, name, &found);
                if (!found) {
                    ps_env_define(env, name, ps_value_undefined());
                }
            }
            hoist_decls(vm, env, node->as.for_of.body);
            break;
        case AST_SWITCH:
            for (size_t i = 0; i < node->as.switch_stmt.case_count; i++) {
                hoist_decls(vm, env, node->as.switch_stmt.cases[i]);
            }
            break;
        case AST_CASE:
            hoist_list(vm, env, node->as.case_stmt.items, node->as.case_stmt.count);
            break;
        case AST_WITH:
            hoist_decls(vm, env, node->as.with_stmt.body);
            break;
        case AST_TRY:
            hoist_decls(vm, env, node->as.try_stmt.try_block);
            hoist_decls(vm, env, node->as.try_stmt.catch_block);
            hoist_decls(vm, env, node->as.try_stmt.finally_block);
            break;
        case AST_LABEL:
            hoist_decls(vm, env, node->as.label_stmt.stmt);
            break;
        default:
            break;
    }
}

/* --------------------------------------------------------- */
/* Program                                                   */
/* --------------------------------------------------------- */

PSValue ps_eval(PSVM *vm, PSAstNode *program) {
    PSValue last = ps_value_undefined();
    PSEvalControl ctl = {0};

    if (vm) vm->current_ast = program;
    hoist_decls(vm, vm->env, program);

    for (size_t i = 0; i < program->as.list.count; i++) {
        last = eval_node(vm, vm->env, program->as.list.items[i], &ctl);
        if (ctl.did_throw) {
            fprintf(stderr, "Uncaught exception\n");
            exit(1);
        }
        if (ctl.did_return) {
            if (vm) vm->current_ast = NULL;
            return last;
        }
    }
    if (vm) vm->current_ast = NULL;
    return last;
}

/* --------------------------------------------------------- */
/* Statements                                                */
/* --------------------------------------------------------- */

static PSValue eval_node(PSVM *vm, PSEnv *env, PSAstNode *node, PSEvalControl *ctl) {
    if (vm) {
        vm->env = env;
        ps_gc_safe_point(vm);
    }
    switch (node->kind) {
        case AST_BLOCK:
            return eval_block(vm, env, node, ctl);

        case AST_VAR_DECL: {
            PSString *name = ps_string_from_utf8(
                node->as.var_decl.id->as.identifier.name,
                node->as.var_decl.id->as.identifier.length
            );

            PSValue val = ps_value_undefined();
            if (node->as.var_decl.init) {
                val = eval_expression(vm, env, node->as.var_decl.init, ctl);
            }

            ps_env_define(env, name, val);
            return ps_value_undefined();
        }

        case AST_EXPR_STMT:
            return eval_expression(vm, env, node->as.expr_stmt.expr, ctl);

        case AST_RETURN: {
            ctl->did_return = 1;
            if (node->as.ret.expr) {
                return eval_expression(vm, env, node->as.ret.expr, ctl);
            }
            return ps_value_undefined();
        }

        case AST_IF: {
            PSValue cond = eval_expression(vm, env, node->as.if_stmt.cond, ctl);
            if (ps_to_boolean(vm, cond)) {
                return eval_node(vm, env, node->as.if_stmt.then_branch, ctl);
            }
            if (node->as.if_stmt.else_branch) {
                return eval_node(vm, env, node->as.if_stmt.else_branch, ctl);
            }
            return ps_value_undefined();
        }

        case AST_WHILE: {
            PSString *loop_label = NULL;
            if (node->as.while_stmt.label) {
                loop_label = ps_string_from_utf8(
                    node->as.while_stmt.label->as.identifier.name,
                    node->as.while_stmt.label->as.identifier.length
                );
            }
            PSValue last = ps_value_undefined();
            while (1) {
                PSValue cond = eval_expression(vm, env, node->as.while_stmt.cond, ctl);
                if (!ps_to_boolean(vm, cond)) break;
                last = eval_node(vm, env, node->as.while_stmt.body, ctl);
                if (ctl->did_throw || ctl->did_return) return last;
                if (ctl->did_break) {
                    if (ctl->break_label) {
                        if (!loop_label || !ps_string_equals(ctl->break_label, loop_label)) {
                            fprintf(stderr, "break label no match\n");
                            return last;
                        }
                        fprintf(stderr, "break label match\n");
                    }
                    ctl->did_break = 0;
                    ctl->break_label = NULL;
                    break;
                }
                if (ctl->did_continue) {
                    if (ctl->continue_label) {
                        if (!loop_label || !ps_string_equals(ctl->continue_label, loop_label)) {
                            return last;
                        }
                    }
                    ctl->did_continue = 0;
                    ctl->continue_label = NULL;
                    continue;
                }
            }
            return last;
        }

        case AST_DO_WHILE: {
            PSString *loop_label = NULL;
            if (node->as.do_while.label) {
                loop_label = ps_string_from_utf8(
                    node->as.do_while.label->as.identifier.name,
                    node->as.do_while.label->as.identifier.length
                );
            }
            PSValue last = ps_value_undefined();
            do {
                last = eval_node(vm, env, node->as.do_while.body, ctl);
                if (ctl->did_throw || ctl->did_return) return last;
                if (ctl->did_break) {
                    if (ctl->break_label) {
                        if (!loop_label || !ps_string_equals(ctl->break_label, loop_label)) {
                            return last;
                        }
                    }
                    ctl->did_break = 0;
                    ctl->break_label = NULL;
                    break;
                }
                if (ctl->did_continue) {
                    if (ctl->continue_label) {
                        if (!loop_label || !ps_string_equals(ctl->continue_label, loop_label)) {
                            return last;
                        }
                    }
                    ctl->did_continue = 0;
                    ctl->continue_label = NULL;
                }
                PSValue cond = eval_expression(vm, env, node->as.do_while.cond, ctl);
                if (!ps_to_boolean(vm, cond)) break;
            } while (1);
            return last;
        }

        case AST_FOR: {
            PSString *loop_label = NULL;
            if (node->as.for_stmt.label) {
                loop_label = ps_string_from_utf8(
                    node->as.for_stmt.label->as.identifier.name,
                    node->as.for_stmt.label->as.identifier.length
                );
            }
            PSValue last = ps_value_undefined();
            if (node->as.for_stmt.init) {
                last = eval_node(vm, env, node->as.for_stmt.init, ctl);
                if (ctl->did_throw || ctl->did_return) return last;
            }
            while (1) {
                if (node->as.for_stmt.test) {
                    PSValue cond = eval_expression(vm, env, node->as.for_stmt.test, ctl);
                    if (!ps_to_boolean(vm, cond)) break;
                }
                last = eval_node(vm, env, node->as.for_stmt.body, ctl);
                if (ctl->did_throw || ctl->did_return) return last;
                if (ctl->did_break) {
                    if (ctl->break_label) {
                        if (!loop_label || !ps_string_equals(ctl->break_label, loop_label)) {
                            return last;
                        }
                    }
                    ctl->did_break = 0;
                    ctl->break_label = NULL;
                    break;
                }
                if (ctl->did_continue) {
                    if (ctl->continue_label) {
                        if (!loop_label || !ps_string_equals(ctl->continue_label, loop_label)) {
                            return last;
                        }
                    }
                    ctl->did_continue = 0;
                    ctl->continue_label = NULL;
                }
                if (node->as.for_stmt.update) {
                    (void)eval_expression(vm, env, node->as.for_stmt.update, ctl);
                }
            }
            return last;
        }

        case AST_FOR_IN: {
            PSString *loop_label = NULL;
            if (node->as.for_in.label) {
                loop_label = ps_string_from_utf8(
                    node->as.for_in.label->as.identifier.name,
                    node->as.for_in.label->as.identifier.length
                );
            }
            PSValue obj_val = eval_expression(vm, env, node->as.for_in.object, ctl);
            if (ctl->did_throw) return ctl->throw_value;
            PSObject *obj = ps_to_object(vm, &obj_val, ctl);
            if (ctl->did_throw) return ctl->throw_value;
            PSNameList list = {0};
            for (PSObject *cur = obj; cur; cur = cur->prototype) {
                if (ps_object_enum_own(cur, ps_collect_enum_name, &list) != 0) {
                    break;
                }
            }

            PSValue last = ps_value_undefined();
            PSString **ordered = list.items;
            size_t ordered_count = list.count;
            PSIndexName *indices = NULL;
            PSString **others = NULL;
            PSString **ordered_alloc = NULL;
            if (obj && obj->kind == PS_OBJ_KIND_ARRAY && list.count > 0) {
                indices = (PSIndexName *)calloc(list.count, sizeof(PSIndexName));
                others = (PSString **)calloc(list.count, sizeof(PSString *));
                if (indices && others) {
                    size_t index_count = 0;
                    size_t other_count = 0;
                    for (size_t i = 0; i < list.count; i++) {
                        PSString *name = list.items[i];
                        if (ps_string_is_length(name)) continue;
                        size_t idx = 0;
                        if (ps_string_to_array_index(name, &idx)) {
                            indices[index_count].name = name;
                            indices[index_count].index = idx;
                            index_count++;
                        } else {
                            others[other_count++] = name;
                        }
                    }
                    qsort(indices, index_count, sizeof(PSIndexName), ps_index_name_compare);
                    ordered_alloc = (PSString **)calloc(index_count + other_count, sizeof(PSString *));
                    if (ordered_alloc) {
                        ordered = ordered_alloc;
                        ordered_count = index_count + other_count;
                        for (size_t i = 0; i < index_count; i++) {
                            ordered[i] = indices[i].name;
                        }
                        for (size_t i = 0; i < other_count; i++) {
                            ordered[index_count + i] = others[i];
                        }
                    }
                }
            }

            for (size_t i = 0; i < ordered_count; i++) {
                PSValue name_val = ps_value_string(ordered[i]);
                if (node->as.for_in.is_var) {
                    if (node->as.for_in.target->kind == AST_IDENTIFIER) {
                        PSString *name = ps_string_from_utf8(
                            node->as.for_in.target->as.identifier.name,
                            node->as.for_in.target->as.identifier.length
                        );
                        ps_env_define(env, name, name_val);
                    }
                } else if (node->as.for_in.target->kind == AST_IDENTIFIER) {
                    PSString *name = ps_string_from_utf8(
                        node->as.for_in.target->as.identifier.name,
                        node->as.for_in.target->as.identifier.length
                    );
                    ps_env_set(env, name, name_val);
                } else if (node->as.for_in.target->kind == AST_MEMBER) {
                    PSValue target_val = eval_expression(vm, env, node->as.for_in.target->as.member.object, ctl);
                    if (ctl->did_throw) return ctl->throw_value;
                    PSObject *target_obj = ps_to_object(vm, &target_val, ctl);
                    if (ctl->did_throw) return ctl->throw_value;
                    PSString *prop = ps_member_key(vm, env, node->as.for_in.target, ctl);
                    if (ctl->did_throw) return ctl->throw_value;
                    ps_object_put(target_obj, prop, name_val);
                    (void)ps_env_update_arguments(env, target_obj, prop, name_val);
                }

                last = eval_node(vm, env, node->as.for_in.body, ctl);
                if (ctl->did_throw || ctl->did_return) break;
                if (ctl->did_break) {
                    if (ctl->break_label) {
                        if (!loop_label || !ps_string_equals(ctl->break_label, loop_label)) {
                            return last;
                        }
                    }
                    ctl->did_break = 0;
                    ctl->break_label = NULL;
                    break;
                }
                if (ctl->did_continue) {
                    if (ctl->continue_label) {
                        if (!loop_label || !ps_string_equals(ctl->continue_label, loop_label)) {
                            return last;
                        }
                    }
                    ctl->did_continue = 0;
                    ctl->continue_label = NULL;
                    continue;
                }
            }
            free(ordered_alloc);
            free(indices);
            free(others);
            free(list.items);
            return last;
        }

        case AST_FOR_OF: {
            PSString *loop_label = NULL;
            if (node->as.for_of.label) {
                loop_label = ps_string_from_utf8(
                    node->as.for_of.label->as.identifier.name,
                    node->as.for_of.label->as.identifier.length
                );
            }
            PSValue obj_val = eval_expression(vm, env, node->as.for_of.object, ctl);
            if (ctl->did_throw) return ctl->throw_value;

            PSString *string_iter = NULL;
            if (obj_val.type == PS_T_STRING) {
                string_iter = obj_val.as.string;
            } else if (obj_val.type == PS_T_OBJECT && obj_val.as.object &&
                       obj_val.as.object->kind == PS_OBJ_KIND_STRING &&
                       obj_val.as.object->internal) {
                PSValue *inner = (PSValue *)obj_val.as.object->internal;
                if (inner && inner->type == PS_T_STRING) {
                    string_iter = inner->as.string;
                }
            }

            PSValue last = ps_value_undefined();
            if (string_iter) {
                size_t len = ps_string_length(string_iter);
                for (size_t i = 0; i < len; i++) {
                    PSString *ch = ps_string_char_at(string_iter, i);
                    (void)ps_assign_for_target(vm, env, node->as.for_of.target,
                                               node->as.for_of.is_var,
                                               ps_value_string(ch), ctl);
                    if (ctl->did_throw) return ctl->throw_value;
                    last = eval_node(vm, env, node->as.for_of.body, ctl);
                    if (ctl->did_throw || ctl->did_return) break;
                    if (ctl->did_break) {
                        if (ctl->break_label) {
                            if (!loop_label || !ps_string_equals(ctl->break_label, loop_label)) {
                                return last;
                            }
                        }
                        ctl->did_break = 0;
                        ctl->break_label = NULL;
                        break;
                    }
                    if (ctl->did_continue) {
                        if (ctl->continue_label) {
                            if (!loop_label || !ps_string_equals(ctl->continue_label, loop_label)) {
                                return last;
                            }
                        }
                        ctl->did_continue = 0;
                        ctl->continue_label = NULL;
                        continue;
                    }
                }
                return last;
            }

            PSObject *obj = ps_to_object(vm, &obj_val, ctl);
            if (ctl->did_throw) return ctl->throw_value;
            if (obj && obj->kind == PS_OBJ_KIND_ARRAY) {
                size_t len = 0;
                int found = 0;
                PSValue len_val = ps_object_get(obj, ps_string_from_cstr("length"), &found);
                if (found) {
                    double num = ps_value_to_number(&len_val);
                    if (!isnan(num) && num >= 0.0) {
                        len = (size_t)num;
                    }
                }
                for (size_t i = 0; i < len; i++) {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%zu", i);
                    PSValue val = ps_object_get(obj, ps_string_from_cstr(buf), NULL);
                    (void)ps_assign_for_target(vm, env, node->as.for_of.target,
                                               node->as.for_of.is_var, val, ctl);
                    if (ctl->did_throw) return ctl->throw_value;
                    last = eval_node(vm, env, node->as.for_of.body, ctl);
                    if (ctl->did_throw || ctl->did_return) break;
                    if (ctl->did_break) {
                        if (ctl->break_label) {
                            if (!loop_label || !ps_string_equals(ctl->break_label, loop_label)) {
                                return last;
                            }
                        }
                        ctl->did_break = 0;
                        ctl->break_label = NULL;
                        break;
                    }
                    if (ctl->did_continue) {
                        if (ctl->continue_label) {
                            if (!loop_label || !ps_string_equals(ctl->continue_label, loop_label)) {
                                return last;
                            }
                        }
                        ctl->did_continue = 0;
                        ctl->continue_label = NULL;
                        continue;
                    }
                }
                return last;
            }

            PSNameList list = {0};
            if (obj) {
                (void)ps_object_enum_own(obj, ps_collect_enum_name, &list);
            }
            for (size_t i = 0; i < list.count; i++) {
                int found = 0;
                PSValue val = ps_object_get_own(obj, list.items[i], &found);
                if (!found) continue;
                (void)ps_assign_for_target(vm, env, node->as.for_of.target,
                                           node->as.for_of.is_var, val, ctl);
                if (ctl->did_throw) return ctl->throw_value;
                last = eval_node(vm, env, node->as.for_of.body, ctl);
                if (ctl->did_throw || ctl->did_return) break;
                if (ctl->did_break) {
                    if (ctl->break_label) {
                        if (!loop_label || !ps_string_equals(ctl->break_label, loop_label)) {
                            free(list.items);
                            return last;
                        }
                    }
                    ctl->did_break = 0;
                    ctl->break_label = NULL;
                    break;
                }
                if (ctl->did_continue) {
                    if (ctl->continue_label) {
                        if (!loop_label || !ps_string_equals(ctl->continue_label, loop_label)) {
                            free(list.items);
                            return last;
                        }
                    }
                    ctl->did_continue = 0;
                    ctl->continue_label = NULL;
                    continue;
                }
            }
            free(list.items);
            return last;
        }

        case AST_SWITCH: {
            PSString *switch_label = NULL;
            if (node->as.switch_stmt.label) {
                switch_label = ps_string_from_utf8(
                    node->as.switch_stmt.label->as.identifier.name,
                    node->as.switch_stmt.label->as.identifier.length
                );
            }
            PSValue disc = eval_expression(vm, env, node->as.switch_stmt.expr, ctl);
            int matched = 0;
            PSValue last = ps_value_undefined();
            for (size_t i = 0; i < node->as.switch_stmt.case_count; i++) {
                PSAstNode *case_node = node->as.switch_stmt.cases[i];
                if (!matched) {
                    if (!case_node->as.case_stmt.test) {
                        matched = 1;
                    } else {
                        PSValue test_val = eval_expression(vm, env, case_node->as.case_stmt.test, ctl);
                        if (ps_strict_equals(&disc, &test_val)) {
                            matched = 1;
                        }
                    }
                }
                if (matched) {
                    for (size_t j = 0; j < case_node->as.case_stmt.count; j++) {
                        last = eval_node(vm, env, case_node->as.case_stmt.items[j], ctl);
                        if (ctl->did_throw || ctl->did_return) return last;
                        if (ctl->did_break) {
                            if (ctl->break_label) {
                                if (!switch_label ||
                                    !ps_string_equals(ctl->break_label, switch_label)) {
                                    return last;
                                }
                            }
                            ctl->did_break = 0;
                            ctl->break_label = NULL;
                            return last;
                        }
                        if (ctl->did_continue) return last;
                    }
                }
            }
            return last;
        }

        case AST_BREAK:
            if (node->as.jump_stmt.label) {
                ctl->break_label = ps_string_from_utf8(
                    node->as.jump_stmt.label->as.identifier.name,
                    node->as.jump_stmt.label->as.identifier.length
                );
            } else {
                ctl->break_label = NULL;
            }
            ctl->did_break = 1;
            return ps_value_undefined();

        case AST_CONTINUE:
            if (node->as.jump_stmt.label) {
                ctl->continue_label = ps_string_from_utf8(
                    node->as.jump_stmt.label->as.identifier.name,
                    node->as.jump_stmt.label->as.identifier.length
                );
            } else {
                ctl->continue_label = NULL;
            }
            ctl->did_continue = 1;
            return ps_value_undefined();

        case AST_LABEL: {
            PSString *label = ps_string_from_utf8(
                node->as.label_stmt.label->as.identifier.name,
                node->as.label_stmt.label->as.identifier.length
            );
            PSValue last = eval_node(vm, env, node->as.label_stmt.stmt, ctl);
            if (ctl->did_break && ctl->break_label &&
                ps_string_equals(ctl->break_label, label)) {
                ctl->did_break = 0;
                ctl->break_label = NULL;
                return last;
            }
            if (ctl->did_continue && ctl->continue_label &&
                ps_string_equals(ctl->continue_label, label)) {
                if (node->as.label_stmt.stmt->kind != AST_WHILE &&
                    node->as.label_stmt.stmt->kind != AST_DO_WHILE &&
                    node->as.label_stmt.stmt->kind != AST_FOR &&
                    node->as.label_stmt.stmt->kind != AST_FOR_IN &&
                    node->as.label_stmt.stmt->kind != AST_FOR_OF) {
                    ctl->did_throw = 1;
                    ctl->throw_value = ps_value_string(ps_string_from_cstr("SyntaxError"));
                    return ctl->throw_value;
                }
                ctl->did_continue = 0;
                ctl->continue_label = NULL;
                return last;
            }
            return last;
        }

        case AST_WITH: {
            PSValue obj_val = eval_expression(vm, env, node->as.with_stmt.object, ctl);
            if (ctl->did_throw) return obj_val;
            PSObject *obj = ps_to_object(vm, &obj_val, ctl);
            if (ctl->did_throw) return ctl->throw_value;
            PSEnv *with_env = ps_env_new(env, obj, 0);
            if (!with_env) return ps_value_undefined();
            PSValue last = eval_node(vm, with_env, node->as.with_stmt.body, ctl);
            ps_env_free(with_env);
            return last;
        }

        case AST_THROW: {
            PSValue v = eval_expression(vm, env, node->as.throw_stmt.expr, ctl);
            ctl->did_throw = 1;
            ctl->throw_value = v;
            return v;
        }

        case AST_TRY: {
            PSValue last = eval_node(vm, env, node->as.try_stmt.try_block, ctl);

            if (ctl->did_throw && node->as.try_stmt.catch_block) {
                PSValue thrown = ctl->throw_value;
                ctl->did_throw = 0;
                PSEnv *catch_env = ps_env_new_object(env);
                if (catch_env && node->as.try_stmt.catch_param) {
                    PSString *name = ps_string_from_utf8(
                        node->as.try_stmt.catch_param->as.identifier.name,
                        node->as.try_stmt.catch_param->as.identifier.length
                    );
                    ps_env_define(catch_env, name, thrown);
                }
                last = eval_node(vm, catch_env ? catch_env : env,
                                 node->as.try_stmt.catch_block, ctl);
                if (catch_env) ps_env_free(catch_env);
            }

            if (node->as.try_stmt.finally_block) {
                PSEvalControl saved = *ctl;
                ctl->did_return = 0;
                ctl->did_break = 0;
                ctl->did_continue = 0;
                ctl->did_throw = 0;
                PSValue fin = eval_node(vm, env, node->as.try_stmt.finally_block, ctl);
                if (ctl->did_return || ctl->did_break || ctl->did_continue || ctl->did_throw) {
                    return fin;
                }
                *ctl = saved;
            }

            return last;
        }

        case AST_FUNCTION_DECL: {
            return ps_value_undefined();
        }

        default:
            fprintf(stderr, "Unsupported statement kind: %d\n", node->kind);
            return ps_value_undefined();
    }
}

/* --------------------------------------------------------- */
/* Expressions                                               */
/* --------------------------------------------------------- */

static PSValue eval_expression(PSVM *vm, PSEnv *env, PSAstNode *node, PSEvalControl *ctl) {
    switch (node->kind) {

        case AST_LITERAL:
            return node->as.literal.value;

        case AST_ARRAY_LITERAL: {
            PSObject *arr = ps_object_new(vm->array_proto ? vm->array_proto : vm->object_proto);
            if (!arr) return ps_value_undefined();
            arr->kind = PS_OBJ_KIND_ARRAY;
            for (size_t i = 0; i < node->as.array_literal.count; i++) {
                PSValue v = eval_expression(vm, env, node->as.array_literal.items[i], ctl);
                if (ctl->did_throw) return v;
                char buf[32];
                snprintf(buf, sizeof(buf), "%zu", i);
                ps_object_define(arr, ps_string_from_cstr(buf), v, PS_ATTR_NONE);
            }
            ps_object_define(arr,
                             ps_string_from_cstr("length"),
                             ps_value_number((double)node->as.array_literal.count),
                             PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
            return ps_value_object(arr);
        }

        case AST_OBJECT_LITERAL: {
            PSObject *obj = ps_object_new(vm->object_proto);
            if (!obj) return ps_value_undefined();
            for (size_t i = 0; i < node->as.object_literal.count; i++) {
                PSValue v = eval_expression(vm, env, node->as.object_literal.props[i].value, ctl);
                if (ctl->did_throw) return v;
                ps_object_define(obj, node->as.object_literal.props[i].key, v, PS_ATTR_NONE);
            }
            return ps_value_object(obj);
        }

        case AST_IDENTIFIER: {
            PSString *name = ps_string_from_utf8(
                node->as.identifier.name,
                node->as.identifier.length
            );
            int found;
            PSValue v = ps_env_get(env, name, &found);
            if (!found) {
                ctl->did_throw = 1;
                ctl->throw_value = ps_vm_make_error(vm, "ReferenceError", "Identifier not defined");
                return ctl->throw_value;
            }
            return v;
        }

        case AST_ASSIGN: {
            PSValue rhs = eval_expression(vm, env, node->as.assign.value, ctl);
            if (ctl->did_throw) return rhs;
            PSValue new_value = rhs;

            if (node->as.assign.op != TOK_ASSIGN) {
                PSValue current = ps_value_undefined();
                if (node->as.assign.target->kind == AST_IDENTIFIER) {
                    PSString *name = ps_string_from_utf8(
                        node->as.assign.target->as.identifier.name,
                        node->as.assign.target->as.identifier.length
                    );
                    int found;
                    current = ps_env_get(env, name, &found);
                } else if (node->as.assign.target->kind == AST_MEMBER) {
                    PSValue obj_val = eval_expression(vm, env, node->as.assign.target->as.member.object, ctl);
                    if (ctl->did_throw) return obj_val;
                    PSObject *obj = ps_to_object(vm, &obj_val, ctl);
                    if (ctl->did_throw) return ctl->throw_value;
                    PSString *prop = ps_member_key(vm, env, node->as.assign.target, ctl);
                    if (ctl->did_throw) return ctl->throw_value;
                    int found;
                    current = ps_object_get(obj, prop, &found);
                }

                switch (node->as.assign.op) {
                    case TOK_PLUS_ASSIGN:
                    {
                        PSValue lprim = ps_to_primitive(vm, current, PS_HINT_NONE);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        PSValue rprim = ps_to_primitive(vm, rhs, PS_HINT_NONE);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        if (lprim.type == PS_T_STRING || rprim.type == PS_T_STRING) {
                            PSString *ls = ps_to_string(vm, lprim);
                            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                            PSString *rs = ps_to_string(vm, rprim);
                            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                            new_value = ps_value_string(ps_string_concat(ls, rs));
                        } else {
                            double ln = ps_to_number(vm, lprim);
                            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                            double rn = ps_to_number(vm, rprim);
                            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                            new_value = ps_value_number(ln + rn);
                        }
                        break;
                    }
                    case TOK_MINUS_ASSIGN:
                    {
                        double ln = ps_to_number(vm, current);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        double rn = ps_to_number(vm, rhs);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        new_value = ps_value_number(ln - rn);
                        break;
                    }
                    case TOK_STAR_ASSIGN:
                    {
                        double ln = ps_to_number(vm, current);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        double rn = ps_to_number(vm, rhs);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        new_value = ps_value_number(ln * rn);
                        break;
                    }
                    case TOK_SLASH_ASSIGN:
                    {
                        double ln = ps_to_number(vm, current);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        double rn = ps_to_number(vm, rhs);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        new_value = ps_value_number(ln / rn);
                        break;
                    }
                    case TOK_PERCENT_ASSIGN:
                    {
                        double ln = ps_to_number(vm, current);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        double rn = ps_to_number(vm, rhs);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        new_value = ps_value_number(fmod(ln, rn));
                        break;
                    }
                    case TOK_SHL_ASSIGN:
                    {
                        int32_t ln = ps_to_int32(vm, &current);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        uint32_t rn = ps_to_uint32(vm, &rhs);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        new_value = ps_value_number((double)(ln << (rn & 31)));
                        break;
                    }
                    case TOK_SHR_ASSIGN:
                    {
                        int32_t ln = ps_to_int32(vm, &current);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        uint32_t rn = ps_to_uint32(vm, &rhs);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        new_value = ps_value_number((double)(ln >> (rn & 31)));
                        break;
                    }
                    case TOK_USHR_ASSIGN:
                    {
                        uint32_t ln = ps_to_uint32(vm, &current);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        uint32_t rn = ps_to_uint32(vm, &rhs);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        new_value = ps_value_number((double)(ln >> (rn & 31)));
                        break;
                    }
                    case TOK_AND_ASSIGN:
                    {
                        int32_t ln = ps_to_int32(vm, &current);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        int32_t rn = ps_to_int32(vm, &rhs);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        new_value = ps_value_number((double)(ln & rn));
                        break;
                    }
                    case TOK_OR_ASSIGN:
                    {
                        int32_t ln = ps_to_int32(vm, &current);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        int32_t rn = ps_to_int32(vm, &rhs);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        new_value = ps_value_number((double)(ln | rn));
                        break;
                    }
                    case TOK_XOR_ASSIGN:
                    {
                        int32_t ln = ps_to_int32(vm, &current);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        int32_t rn = ps_to_int32(vm, &rhs);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        new_value = ps_value_number((double)(ln ^ rn));
                        break;
                    }
                    default:
                        break;
                }
            }

            if (node->as.assign.target->kind == AST_IDENTIFIER) {
                PSString *name = ps_string_from_utf8(
                    node->as.assign.target->as.identifier.name,
                    node->as.assign.target->as.identifier.length
                );
                ps_env_set(env, name, new_value);
                return new_value;
            }
            if (node->as.assign.target->kind == AST_MEMBER) {
                PSValue obj_val = eval_expression(vm, env, node->as.assign.target->as.member.object, ctl);
                if (ctl->did_throw) return obj_val;
                PSObject *obj = ps_to_object(vm, &obj_val, ctl);
                if (ctl->did_throw) return ctl->throw_value;
                PSString *prop = ps_member_key(vm, env, node->as.assign.target, ctl);
                if (ctl->did_throw) return ctl->throw_value;
                if (obj->kind == PS_OBJ_KIND_ARRAY && ps_string_is_length(prop)) {
                    int ok = ps_array_set_length(vm, obj, new_value, ctl);
                    if (ok < 0) return ctl->throw_value;
                    return new_value;
                }
                ps_object_put(obj, prop, new_value);
                ps_array_update_length(obj, prop);
                (void)ps_env_update_arguments(env, obj, prop, new_value);
                return new_value;
            }
            fprintf(stderr, "Invalid assignment target\n");
            return ps_value_undefined();
        }

        case AST_BINARY: {
            if (node->as.binary.op == TOK_AND_AND) {
                PSValue left = eval_expression(vm, env, node->as.binary.left, ctl);
                if (ctl->did_throw) return left;
                if (!ps_to_boolean(vm, left)) return left;
                return eval_expression(vm, env, node->as.binary.right, ctl);
            }
            if (node->as.binary.op == TOK_OR_OR) {
                PSValue left = eval_expression(vm, env, node->as.binary.left, ctl);
                if (ctl->did_throw) return left;
                if (ps_to_boolean(vm, left)) return left;
                return eval_expression(vm, env, node->as.binary.right, ctl);
            }

            if (node->as.binary.op == TOK_COMMA) {
                (void)eval_expression(vm, env, node->as.binary.left, ctl);
                if (ctl->did_throw) return ctl->throw_value;
                return eval_expression(vm, env, node->as.binary.right, ctl);
            }

            PSValue left  = eval_expression(vm, env, node->as.binary.left, ctl);
            if (ctl->did_throw) return left;
            PSValue right = eval_expression(vm, env, node->as.binary.right, ctl);
            if (ctl->did_throw) return right;

            switch (node->as.binary.op) {
                case TOK_PLUS:
                {
                    PSValue lprim = ps_to_primitive(vm, left, PS_HINT_NONE);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    PSValue rprim = ps_to_primitive(vm, right, PS_HINT_NONE);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    if (lprim.type == PS_T_STRING || rprim.type == PS_T_STRING) {
                        PSString *ls = ps_to_string(vm, lprim);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        PSString *rs = ps_to_string(vm, rprim);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        return ps_value_string(ps_string_concat(ls, rs));
                    }
                    return ps_value_number(ps_to_number(vm, lprim) + ps_to_number(vm, rprim));
                }
                case TOK_MINUS:
                {
                    double ln = ps_to_number(vm, left);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    double rn = ps_to_number(vm, right);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    return ps_value_number(ln - rn);
                }
                case TOK_STAR:
                {
                    double ln = ps_to_number(vm, left);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    double rn = ps_to_number(vm, right);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    return ps_value_number(ln * rn);
                }
                case TOK_SLASH:
                {
                    double ln = ps_to_number(vm, left);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    double rn = ps_to_number(vm, right);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    return ps_value_number(ln / rn);
                }
                case TOK_PERCENT:
                {
                    double ln = ps_to_number(vm, left);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    double rn = ps_to_number(vm, right);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    return ps_value_number(fmod(ln, rn));
                }
                case TOK_LT:
                case TOK_LTE:
                case TOK_GT:
                case TOK_GTE: {
                    PSValue lprim = ps_to_primitive(vm, left, PS_HINT_NUMBER);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    PSValue rprim = ps_to_primitive(vm, right, PS_HINT_NUMBER);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    if (lprim.type == PS_T_STRING && rprim.type == PS_T_STRING) {
                        int cmp = ps_string_compare(lprim.as.string, rprim.as.string);
                        if (node->as.binary.op == TOK_LT) return ps_value_boolean(cmp < 0);
                        if (node->as.binary.op == TOK_LTE) return ps_value_boolean(cmp <= 0);
                        if (node->as.binary.op == TOK_GT) return ps_value_boolean(cmp > 0);
                        return ps_value_boolean(cmp >= 0);
                    }
                    double ln = ps_to_number(vm, lprim);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    double rn = ps_to_number(vm, rprim);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    if (ps_value_is_nan(ln) || ps_value_is_nan(rn)) return ps_value_boolean(0);
                    if (node->as.binary.op == TOK_LT) return ps_value_boolean(ln < rn);
                    if (node->as.binary.op == TOK_LTE) return ps_value_boolean(ln <= rn);
                    if (node->as.binary.op == TOK_GT) return ps_value_boolean(ln > rn);
                    return ps_value_boolean(ln >= rn);
                }
                case TOK_EQ:
                {
                    int eq = ps_abstract_equals(vm, &left, &right, ctl);
                    if (ctl->did_throw) return ctl->throw_value;
                    return ps_value_boolean(eq);
                }
                case TOK_NEQ:
                {
                    int eq = ps_abstract_equals(vm, &left, &right, ctl);
                    if (ctl->did_throw) return ctl->throw_value;
                    return ps_value_boolean(!eq);
                }
                case TOK_STRICT_EQ:
                    return ps_value_boolean(ps_strict_equals(&left, &right));
                case TOK_STRICT_NEQ:
                    return ps_value_boolean(!ps_strict_equals(&left, &right));
                case TOK_AND:
                {
                    int32_t ln = ps_to_int32(vm, &left);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    int32_t rn = ps_to_int32(vm, &right);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    return ps_value_number((double)(ln & rn));
                }
                case TOK_OR:
                {
                    int32_t ln = ps_to_int32(vm, &left);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    int32_t rn = ps_to_int32(vm, &right);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    return ps_value_number((double)(ln | rn));
                }
                case TOK_XOR:
                {
                    int32_t ln = ps_to_int32(vm, &left);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    int32_t rn = ps_to_int32(vm, &right);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    return ps_value_number((double)(ln ^ rn));
                }
                case TOK_SHL:
                {
                    int32_t ln = ps_to_int32(vm, &left);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    uint32_t rn = ps_to_uint32(vm, &right);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    return ps_value_number((double)(ln << (rn & 31)));
                }
                case TOK_SHR:
                {
                    int32_t ln = ps_to_int32(vm, &left);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    uint32_t rn = ps_to_uint32(vm, &right);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    return ps_value_number((double)(ln >> (rn & 31)));
                }
                case TOK_USHR:
                {
                    uint32_t ln = ps_to_uint32(vm, &left);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    uint32_t rn = ps_to_uint32(vm, &right);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    return ps_value_number((double)(ln >> (rn & 31)));
                }
                default:
                    fprintf(stderr, "Unsupported binary operator\n");
                    return ps_value_undefined();
            }
        }

        case AST_UNARY: {
            PSValue v = ps_value_undefined();
            if (node->as.unary.op == TOK_TYPEOF &&
                node->as.unary.expr->kind == AST_IDENTIFIER) {
                PSAstNode *id = node->as.unary.expr;
                PSString *name_id = ps_string_from_utf8(
                    id->as.identifier.name,
                    id->as.identifier.length
                );
                int found;
                v = ps_env_get(env, name_id, &found);
                if (!found) {
                    return ps_value_string(ps_string_from_cstr("undefined"));
                }
            } else {
                v = eval_expression(vm, env, node->as.unary.expr, ctl);
                if (ctl->did_throw) return v;
            }
            switch (node->as.unary.op) {
                case TOK_NOT:
                    return ps_value_boolean(!ps_to_boolean(vm, v));
                case TOK_BIT_NOT:
                    return ps_value_number((double)(~ps_to_int32(vm, &v)));
                case TOK_PLUS:
                {
                    double num = ps_to_number(vm, v);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    return ps_value_number(num);
                }
                case TOK_MINUS:
                {
                    double num = ps_to_number(vm, v);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    return ps_value_number(-num);
                }
                case TOK_TYPEOF: {
                    const char *name = "undefined";
                    switch (v.type) {
                        case PS_T_UNDEFINED: name = "undefined"; break;
                        case PS_T_NULL: name = "object"; break;
                        case PS_T_BOOLEAN: name = "boolean"; break;
                        case PS_T_NUMBER: name = "number"; break;
                        case PS_T_STRING: name = "string"; break;
                        case PS_T_OBJECT:
                            name = (v.as.object && v.as.object->kind == PS_OBJ_KIND_FUNCTION)
                                ? "function"
                                : "object";
                            break;
                    }
                    return ps_value_string(ps_string_from_cstr(name));
                }
                case TOK_VOID:
                    return ps_value_undefined();
                case TOK_DELETE:
                    if (node->as.unary.expr->kind == AST_MEMBER) {
                        PSAstNode *mem = node->as.unary.expr;
                        PSValue obj_val = eval_expression(vm, env, mem->as.member.object, ctl);
                        if (ctl->did_throw) return obj_val;
                        PSObject *obj = ps_to_object(vm, &obj_val, ctl);
                        if (ctl->did_throw) return ctl->throw_value;
                        PSString *prop = ps_member_key(vm, env, mem, ctl);
                        if (ctl->did_throw) return ctl->throw_value;
                        int deleted = 0;
                        int ok = ps_object_delete(obj, prop, &deleted);
                        return ps_value_boolean(ok ? 1 : 0);
                    }
                    return ps_value_boolean(0);
                default:
                    return ps_value_undefined();
            }
        }

        case AST_UPDATE: {
            PSAstNode *target = node->as.update.expr;
            PSValue current = ps_value_undefined();
            if (target->kind == AST_IDENTIFIER) {
                PSString *name = ps_string_from_utf8(
                    target->as.identifier.name,
                    target->as.identifier.length
                );
                int found;
                current = ps_env_get(env, name, &found);
                double num = ps_to_number(vm, current);
                if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                double new_num = (node->as.update.op == TOK_PLUS_PLUS) ? num + 1 : num - 1;
                PSValue new_val = ps_value_number(new_num);
                ps_env_set(env, name, new_val);
                return node->as.update.is_prefix ? new_val : current;
            }
            if (target->kind == AST_MEMBER) {
                PSValue obj_val = eval_expression(vm, env, target->as.member.object, ctl);
                if (ctl->did_throw) return obj_val;
                PSObject *obj = ps_to_object(vm, &obj_val, ctl);
                if (ctl->did_throw) return ctl->throw_value;
                PSString *prop = ps_member_key(vm, env, target, ctl);
                if (ctl->did_throw) return ctl->throw_value;
                int found;
                current = ps_object_get(obj, prop, &found);
                double num = ps_to_number(vm, current);
                if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                double new_num = (node->as.update.op == TOK_PLUS_PLUS) ? num + 1 : num - 1;
                PSValue new_val = ps_value_number(new_num);
                ps_object_put(obj, prop, new_val);
                ps_array_update_length(obj, prop);
                (void)ps_env_update_arguments(env, obj, prop, new_val);
                return node->as.update.is_prefix ? new_val : current;
            }
            return ps_value_undefined();
        }

        case AST_CONDITIONAL: {
            PSValue cond = eval_expression(vm, env, node->as.conditional.cond, ctl);
            if (ctl->did_throw) return cond;
            if (ps_to_boolean(vm, cond)) {
                return eval_expression(vm, env, node->as.conditional.then_expr, ctl);
            }
            return eval_expression(vm, env, node->as.conditional.else_expr, ctl);
        }

        case AST_MEMBER: {
            PSValue obj_val = eval_expression(vm, env, node->as.member.object, ctl);
            if (ctl->did_throw) return obj_val;
            PSObject *obj = ps_to_object(vm, &obj_val, ctl);
            if (ctl->did_throw) return ctl->throw_value;
            PSString *prop = ps_member_key(vm, env, node, ctl);
            if (ctl->did_throw) return ctl->throw_value;

            int found;
            return ps_object_get(obj, prop, &found);
        }

        case AST_CALL: {
            if (node->as.call.callee->kind == AST_IDENTIFIER) {
                PSAstNode *id = node->as.call.callee;
                if (id->as.identifier.length == 4 &&
                    strncmp(id->as.identifier.name, "eval", 4) == 0) {
                    if (!PS_ENABLE_EVAL) {
                        ctl->did_throw = 1;
                        ctl->throw_value = ps_vm_make_error(vm, "EvalError", "eval is disabled");
                        return ctl->throw_value;
                    }
                    PSValue *args = NULL;
                    if (!ps_eval_args(vm, env, node->as.call.args, node->as.call.argc, &args, ctl)) {
                        return ctl->did_throw ? ctl->throw_value : ps_value_undefined();
                    }
                    PSValue result = ps_value_undefined();
                    if (node->as.call.argc > 0) {
                        if (args[0].type == PS_T_STRING) {
                            result = ps_eval_source(vm, env, args[0].as.string, ctl);
                        } else {
                            result = args[0];
                        }
                    }
                    free(args);
                    return result;
                }
            }

            PSValue this_val = ps_value_undefined();
            PSValue callee = ps_value_undefined();

            if (node->as.call.callee->kind == AST_MEMBER) {
                PSAstNode *member = node->as.call.callee;
                PSValue obj_val = eval_expression(vm, env, member->as.member.object, ctl);
                if (ctl->did_throw) return obj_val;
                PSObject *obj = ps_to_object(vm, &obj_val, ctl);
                if (ctl->did_throw) return ctl->throw_value;
                PSString *prop = ps_member_key(vm, env, member, ctl);
                if (ctl->did_throw) return ctl->throw_value;
                int found;
                callee = ps_object_get(obj, prop, &found);
                this_val = ps_value_object(obj);
            } else {
                callee = eval_expression(vm, env, node->as.call.callee, ctl);
                if (ctl->did_throw) return callee;
                this_val = ps_value_object(vm->global);
            }

            if (callee.type != PS_T_OBJECT) {
                fprintf(stderr, "Call of non-object\n");
                return ps_value_undefined();
            }

            PSFunction *func = ps_function_from_object(callee.as.object);
            if (!func) {
                fprintf(stderr, "Not a callable object\n");
                return ps_value_undefined();
            }

            PSValue *args = NULL;
            if (!ps_eval_args(vm, env, node->as.call.args, node->as.call.argc, &args, ctl)) {
                return ctl->did_throw ? ctl->throw_value : ps_value_undefined();
            }
            int did_throw = 0;
            PSValue throw_value = ps_value_undefined();
            PSValue result = ps_eval_call_function(vm, env, callee.as.object, this_val,
                                                   (int)node->as.call.argc, args,
                                                   &did_throw, &throw_value);
            free(args);
            if (did_throw) {
                ctl->did_throw = 1;
                ctl->throw_value = throw_value;
                return throw_value;
            }
            return result;
        }

        case AST_NEW: {
            PSValue callee = eval_expression(vm, env, node->as.new_expr.callee, ctl);
            if (ctl->did_throw) return callee;

            if (callee.type != PS_T_OBJECT) {
                ctl->did_throw = 1;
                ctl->throw_value = ps_vm_make_error(vm, "TypeError", "Constructor is not an object");
                return ctl->throw_value;
            }

            PSObject *ctor_obj = callee.as.object;
            PSObject *target_obj = ctor_obj;
            PSValue bound_target = ps_object_get(ctor_obj,
                                                 ps_string_from_cstr("bound_target"),
                                                 NULL);
            if (bound_target.type == PS_T_OBJECT && bound_target.as.object) {
                target_obj = bound_target.as.object;
            }

            PSFunction *func = ps_function_from_object(target_obj);
            if (!func) {
                ctl->did_throw = 1;
                ctl->throw_value = ps_vm_make_error(vm, "TypeError", "Constructor is not callable");
                return ctl->throw_value;
            }

            PSObject *proto = vm->object_proto;
            int found = 0;
            PSValue proto_val = ps_object_get(target_obj, ps_string_from_cstr("prototype"), &found);
            if (found && proto_val.type == PS_T_OBJECT && proto_val.as.object) {
                proto = proto_val.as.object;
            }

            PSObject *instance = ps_object_new(proto);
            if (!instance) return ps_value_undefined();
            if (proto == vm->boolean_proto) instance->kind = PS_OBJ_KIND_BOOLEAN;
            if (proto == vm->number_proto) instance->kind = PS_OBJ_KIND_NUMBER;
            if (proto == vm->string_proto) instance->kind = PS_OBJ_KIND_STRING;
            if (proto == vm->array_proto) instance->kind = PS_OBJ_KIND_ARRAY;
            if (proto == vm->date_proto) instance->kind = PS_OBJ_KIND_DATE;
            if (proto == vm->regexp_proto) instance->kind = PS_OBJ_KIND_REGEXP;

            PSValue *args = NULL;
            if (!ps_eval_args(vm, env, node->as.new_expr.args, node->as.new_expr.argc, &args, ctl)) {
                return ctl->did_throw ? ctl->throw_value : ps_value_undefined();
            }

            PSValue *bound_args = NULL;
            size_t bound_len = 0;
            PSValue bound_args_val = ps_object_get(ctor_obj,
                                                   ps_string_from_cstr("bound_args"),
                                                   NULL);
            if (bound_args_val.type == PS_T_OBJECT && bound_args_val.as.object) {
                if (!ps_collect_bound_args(bound_args_val.as.object, &bound_args, &bound_len)) {
                    free(args);
                    ctl->did_throw = 1;
                    ctl->throw_value = ps_vm_make_error(vm, "TypeError", "Out of memory");
                    return ctl->throw_value;
                }
            }

            size_t total = bound_len + node->as.new_expr.argc;
            PSValue *all_args = NULL;
            if (total > 0) {
                all_args = (PSValue *)calloc(total, sizeof(PSValue));
                if (!all_args) {
                    free(args);
                    free(bound_args);
                    ctl->did_throw = 1;
                    ctl->throw_value = ps_vm_make_error(vm, "TypeError", "Out of memory");
                    return ctl->throw_value;
                }
                for (size_t i = 0; i < bound_len; i++) {
                    all_args[i] = bound_args ? bound_args[i] : ps_value_undefined();
                }
                for (size_t i = 0; i < node->as.new_expr.argc; i++) {
                    all_args[bound_len + i] = args[i];
                }
            }
            free(bound_args);
            free(args);

            int did_throw = 0;
            PSValue throw_value = ps_value_undefined();
            int prev_constructing = vm ? vm->is_constructing : 0;
            if (vm) vm->is_constructing = 1;
            PSValue result = ps_eval_call_function(vm, env, target_obj,
                                                   ps_value_object(instance),
                                                   (int)total, all_args,
                                                   &did_throw, &throw_value);
            if (vm) vm->is_constructing = prev_constructing;
            free(all_args);
            if (did_throw) {
                ctl->did_throw = 1;
                ctl->throw_value = throw_value;
                return throw_value;
            }
            if (result.type == PS_T_OBJECT) return result;
            return ps_value_object(instance);
        }

        default:
            fprintf(stderr, "Unsupported expression kind: %d\n", node->kind);
            return ps_value_undefined();
    }
}

static PSValue eval_block(PSVM *vm, PSEnv *env, PSAstNode *node, PSEvalControl *ctl) {
    PSValue last = ps_value_undefined();
    for (size_t i = 0; i < node->as.list.count; i++) {
        last = eval_node(vm, env, node->as.list.items[i], ctl);
        if (ctl->did_return || ctl->did_break || ctl->did_continue || ctl->did_throw) {
            return last;
        }
    }
    return last;
}
