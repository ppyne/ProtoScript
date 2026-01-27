#include "ps_ast.h"
#include "ps_vm.h"
#include "ps_eval.h"
#include "ps_object.h"
#include "ps_array.h"
#include "ps_value.h"
#include "ps_string.h"
#include "ps_lexer.h"
#include "ps_parser.h"
#include "ps_env.h"
#include "ps_function.h"
#include "ps_config.h"
#include "ps_gc.h"
#include "ps_buffer.h"
#include "ps_numeric_map.h"
#include "ps_expr_bc.h"
#include "ps_stmt_bc.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#define PS_PERF_POLL(vm) do { \
    if ((vm) && (vm)->perf_dump_interval_ms) { \
        ps_vm_perf_poll(vm); \
    } \
} while (0)

static uint64_t ps_eval_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000ULL);
}

static int ps_eval_trace_enabled = -1;

static void ps_eval_trace_poll(PSVM *vm, PSAstNode *node) {
    static int init = 0;
    static uint64_t interval_ms = 0;
    static uint64_t next_ms = 0;
    static uint64_t trace_every = 0;
    if (!vm) return;
    if (!init) {
        const char *env_every = getenv("PS_EVAL_TRACE_EVERY");
        if (env_every && env_every[0]) {
            char *end_every = NULL;
            unsigned long long v = strtoull(env_every, &end_every, 10);
            if (end_every && end_every != env_every && v > 0) {
                trace_every = (uint64_t)v;
            }
        }
        const char *env = getenv("PS_EVAL_TRACE_MS");
        if (env && env[0]) {
            char *end = NULL;
            unsigned long long v = strtoull(env, &end, 10);
            if (end && end != env && v > 0) {
                interval_ms = (uint64_t)v;
                next_ms = ps_eval_now_ms() + interval_ms;
            }
        }
        ps_eval_trace_enabled = (trace_every > 0 || interval_ms > 0) ? 1 : 0;
        init = 1;
    }
    if (trace_every > 0) {
        if (vm->perf.eval_node_count % trace_every != 0) return;
    } else if (interval_ms > 0) {
        uint64_t now = ps_eval_now_ms();
        if (now < next_ms) return;
        next_ms = now + interval_ms;
    } else {
        return;
    }
    if (node && node->line && node->column) {
        if (node->source_path) {
            fprintf(stderr, "evalTrace %s:%zu:%zu kind=%d\n",
                    node->source_path, node->line, node->column, (int)node->kind);
        } else {
            fprintf(stderr, "evalTrace <unknown>:%zu:%zu kind=%d\n",
                    node->line, node->column, (int)node->kind);
        }
    } else {
        fprintf(stderr, "evalTrace <unknown> kind=%d\n", node ? (int)node->kind : -1);
    }
}

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
static PSValue eval_expression_inner(PSVM *vm,
                                     PSEnv *env,
                                     PSAstNode *node,
                                     PSEvalControl *ctl,
                                     int allow_bc);
static PSStmtBC *ps_stmt_bc_compile(PSAstNode *node);
static PSValue ps_stmt_bc_execute(PSVM *vm, PSEnv *env, PSFunction *func, PSStmtBC *bc, PSEvalControl *ctl);
static PSValue eval_block(PSVM *vm, PSEnv *env, PSAstNode *node, PSEvalControl *ctl);
static PSValue ps_eval_source(PSVM *vm, PSEnv *env, PSString *source, PSEvalControl *ctl);
static void hoist_decls(PSVM *vm, PSEnv *env, PSAstNode *node);
static PSString *ps_identifier_string(PSAstNode *node);
static int ps_identifier_cached_get(PSEnv *env,
                                    PSAstNode *id,
                                    PSValue *out,
                                    int *found);
static int ps_string_to_index_size(PSString *name, size_t *out_index);
static void ps_array_update_length(PSObject *obj, PSString *prop);
static int ps_array_write_index_value(PSVM *vm,
                                      PSObject *obj,
                                      PSValue key_val,
                                      PSValue value,
                                      PSEvalControl *ctl);
static PSValue ps_eval_binary_values(PSVM *vm,
                                     int op,
                                     PSValue left,
                                     PSValue right,
                                     PSEvalControl *ctl);

static int ps_value_is_nan(double v) {
    return isnan(v);
}

static void ps_print_string(FILE *out, const PSString *s) {
    if (!out || !s || !s->utf8 || s->byte_len == 0) return;
    fwrite(s->utf8, 1, s->byte_len, out);
}

static void ps_print_uncaught(PSVM *vm, PSValue thrown) {
    PSString *name = NULL;
    PSString *message = NULL;

    if (thrown.type == PS_T_OBJECT && thrown.as.object) {
        int found = 0;
        PSValue name_val = ps_object_get(thrown.as.object, ps_string_from_cstr("name"), &found);
        if (found) {
            name = ps_value_to_string(&name_val);
        }
        found = 0;
        PSValue msg_val = ps_object_get(thrown.as.object, ps_string_from_cstr("message"), &found);
        if (found) {
            message = ps_value_to_string(&msg_val);
        }
    }

    if (!message) {
        message = ps_value_to_string(&thrown);
    }

    if (vm && vm->current_node && vm->current_node->line && vm->current_node->column) {
        if (vm->current_node->source_path) {
            fprintf(stderr, "%s:%zu:%zu ",
                    vm->current_node->source_path,
                    vm->current_node->line,
                    vm->current_node->column);
        } else {
            fprintf(stderr, "%zu:%zu ",
                    vm->current_node->line,
                    vm->current_node->column);
        }
    }

    fprintf(stderr, "Uncaught ");
    if (name) {
        ps_print_string(stderr, name);
        if (message && message->byte_len > 0) {
            fprintf(stderr, ": ");
            ps_print_string(stderr, message);
        }
    } else if (message) {
        ps_print_string(stderr, message);
    } else {
        fprintf(stderr, "exception");
    }
    fprintf(stderr, "\n");
}

static char *ps_alloc_cstr_from_psstring(const PSString *s) {
    if (!s || !s->utf8 || s->byte_len == 0) return NULL;
    char *out = (char *)malloc(s->byte_len + 1);
    if (!out) return NULL;
    memcpy(out, s->utf8, s->byte_len);
    out[s->byte_len] = '\0';
    return out;
}

static const char *ps_object_kind_label(const PSObject *obj) {
    if (!obj) return "null";
    switch (obj->kind) {
        case PS_OBJ_KIND_FUNCTION: return "Function";
        case PS_OBJ_KIND_ARRAY: return "Array";
        case PS_OBJ_KIND_STRING: return "String";
        case PS_OBJ_KIND_NUMBER: return "Number";
        case PS_OBJ_KIND_BOOLEAN: return "Boolean";
        case PS_OBJ_KIND_DATE: return "Date";
        case PS_OBJ_KIND_REGEXP: return "RegExp";
        default: return "Object";
    }
}

static char *ps_format_call_target_name(PSAstNode *callee) {
    if (!callee) return NULL;
    if (callee->kind == AST_IDENTIFIER) {
        PSString *name = ps_identifier_string(callee);
        return ps_alloc_cstr_from_psstring(name);
    }
    if (callee->kind != AST_MEMBER) {
        return NULL;
    }

    PSAstNode *obj = callee->as.member.object;
    PSAstNode *prop = callee->as.member.property;
    int computed = callee->as.member.computed;
    char *obj_name = NULL;
    char *prop_name = NULL;

    if (obj && obj->kind == AST_IDENTIFIER) {
        obj_name = ps_alloc_cstr_from_psstring(ps_identifier_string(obj));
    }

    if (prop) {
        if (prop->kind == AST_IDENTIFIER) {
            prop_name = ps_alloc_cstr_from_psstring(ps_identifier_string(prop));
        } else if (prop->kind == AST_LITERAL &&
                   prop->as.literal.value.type == PS_T_STRING) {
            prop_name = ps_alloc_cstr_from_psstring(prop->as.literal.value.as.string);
        }
    }

    if (obj_name && prop_name) {
        size_t obj_len = strlen(obj_name);
        size_t prop_len = strlen(prop_name);
        size_t extra = computed ? 2 : 1;
        char *out = (char *)malloc(obj_len + prop_len + extra + 1);
        if (out) {
            memcpy(out, obj_name, obj_len);
            if (computed) {
                out[obj_len] = '[';
                memcpy(out + obj_len + 1, prop_name, prop_len);
                out[obj_len + 1 + prop_len] = ']';
                out[obj_len + 2 + prop_len] = '\0';
            } else {
                out[obj_len] = '.';
                memcpy(out + obj_len + 1, prop_name, prop_len);
                out[obj_len + 1 + prop_len] = '\0';
            }
        }
        free(obj_name);
        free(prop_name);
        return out;
    }

    if (obj_name) return obj_name;
    if (prop_name) return prop_name;
    return NULL;
}

static void ps_vm_push_frame(PSVM *vm, PSFunction *func) {
    if (!vm) return;
    if (vm->stack_depth == vm->stack_capacity) {
        size_t new_cap = vm->stack_capacity ? vm->stack_capacity * 2 : 8;
        PSStackFrame *next = (PSStackFrame *)realloc(vm->stack_frames,
                                                     sizeof(PSStackFrame) * new_cap);
        if (!next) return;
        vm->stack_frames = next;
        vm->stack_capacity = new_cap;
    }
    PSStackFrame *frame = &vm->stack_frames[vm->stack_depth++];
    frame->function_name = func ? func->name : NULL;
    frame->line = vm->current_node ? vm->current_node->line : 0;
    frame->column = vm->current_node ? vm->current_node->column : 0;
    frame->source_path = vm->current_node ? vm->current_node->source_path : NULL;
}

static void ps_vm_pop_frame(PSVM *vm) {
    if (!vm || vm->stack_depth == 0) return;
    vm->stack_depth--;
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

    PSAstNode *program = ps_parse_with_path(buf, NULL);
    free(buf);
    if (!program) {
        ctl->did_throw = 1;
        ctl->throw_value = ps_vm_make_error(vm, "SyntaxError", "eval parse failed");
        return ctl->throw_value;
    }

    PSAstNode *prev_ast = vm ? vm->current_ast : NULL;
    PSAstNode *prev_root = vm ? vm->root_ast : NULL;
    PSAstNode *prev_node = vm ? vm->current_node : NULL;
    if (vm) {
        vm->current_ast = program;
        vm->root_ast = program;
    }

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
        if (vm) {
            vm->current_ast = prev_ast;
            vm->root_ast = prev_root;
            vm->current_node = prev_node;
        }
        return ctl->throw_value;
    }

    ps_ast_free(program);
    if (vm) {
        vm->current_ast = prev_ast;
        vm->root_ast = prev_root;
        vm->current_node = prev_node;
    }
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

static uint8_t ps_clamp_byte(double num);
static uint32_t ps_clamp_u32(double num);
static int ps_buffer_index_from_prop(PSObject *obj, PSString *prop, size_t *out_index);
static int ps_buffer32_index_from_prop(PSObject *obj, PSString *prop, size_t *out_index);
static int ps_buffer_read_index(PSVM *vm,
                                PSObject *obj,
                                PSString *prop,
                                PSValue *out_value,
                                PSEvalControl *ctl);
static int ps_buffer_write_index(PSVM *vm,
                                 PSObject *obj,
                                 PSString *prop,
                                 PSValue value,
                                 PSEvalControl *ctl);
static int ps_buffer32_read_index(PSVM *vm,
                                  PSObject *obj,
                                  PSString *prop,
                                  PSValue *out_value,
                                  PSEvalControl *ctl);
static int ps_buffer32_write_index(PSVM *vm,
                                   PSObject *obj,
                                   PSString *prop,
                                   PSValue value,
                                   PSEvalControl *ctl);
static int ps_value_to_index(PSVM *vm, PSValue value, size_t *out_index, PSEvalControl *ctl);
static int ps_buffer_read_index_value(PSVM *vm,
                                      PSObject *obj,
                                      PSValue key_val,
                                      PSValue *out_value,
                                      PSEvalControl *ctl);
static int ps_buffer_write_index_value(PSVM *vm,
                                       PSObject *obj,
                                       PSValue key_val,
                                       PSValue value,
                                       PSEvalControl *ctl);
static int ps_buffer32_read_index_value(PSVM *vm,
                                        PSObject *obj,
                                        PSValue key_val,
                                        PSValue *out_value,
                                        PSEvalControl *ctl);
static int ps_buffer32_write_index_value(PSVM *vm,
                                         PSObject *obj,
                                         PSValue key_val,
                                         PSValue value,
                                         PSEvalControl *ctl);

static void ps_define_script_function_props(PSObject *fn, PSString *name, size_t param_count) {
    if (!fn) return;
    PSFunction *func = ps_function_from_object(fn);
    if (func) {
        func->name = name;
    }
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

static int ps_string_equals_cstr(const PSString *s, const char *lit) {
    if (!s || !lit) return 0;
    size_t len = strlen(lit);
    if (s->byte_len != len) return 0;
    return memcmp(s->utf8, lit, len) == 0;
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

static int32_t ps_to_int32_num(double num) {
    if (ps_value_is_nan(num) || num == 0.0 || isinf(num)) return 0;
    double sign = num < 0 ? -1.0 : 1.0;
    double abs = floor(fabs(num));
    double val = fmod(sign * abs, 4294967296.0);
    if (val >= 2147483648.0) val -= 4294967296.0;
    return (int32_t)val;
}

static uint32_t ps_to_uint32_num(double num) {
    if (ps_value_is_nan(num) || num == 0.0 || isinf(num)) return 0;
    double sign = num < 0 ? -1.0 : 1.0;
    double abs = floor(fabs(num));
    double val = fmod(sign * abs, 4294967296.0);
    if (val < 0) val += 4294967296.0;
    return (uint32_t)val;
}

static double ps_to_number_fast(PSVM *vm, PSValue value) {
    if (value.type == PS_T_NUMBER) return value.as.number;
    return ps_to_number(vm, value);
}

static int32_t ps_to_int32_fast(PSVM *vm, PSValue value) {
    if (value.type == PS_T_NUMBER) return ps_to_int32_num(value.as.number);
    return ps_to_int32(vm, &value);
}

static uint32_t ps_to_uint32_fast(PSVM *vm, PSValue value) {
    if (value.type == PS_T_NUMBER) return ps_to_uint32_num(value.as.number);
    return ps_to_uint32(vm, &value);
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

static PSString *ps_identifier_string(PSAstNode *node) {
    if (!node || node->kind != AST_IDENTIFIER) return NULL;
    if (node->as.identifier.str) return node->as.identifier.str;
    PSString *s = ps_string_from_utf8(node->as.identifier.name, node->as.identifier.length);
    node->as.identifier.str = s;
    return s;
}

static int ps_literal_string_equals(PSAstNode *node, const char *lit, size_t len) {
    if (!node || node->kind != AST_LITERAL) return 0;
    if (node->as.literal.value.type != PS_T_STRING) return 0;
    PSString *s = node->as.literal.value.as.string;
    if (!s || s->byte_len != len) return 0;
    return memcmp(s->utf8, lit, len) == 0;
}

static size_t ps_number_to_chars(double num, char *buf, size_t cap) {
    if (isnan(num)) {
        return (size_t)snprintf(buf, cap, "NaN");
    }
    if (isinf(num)) {
        return (size_t)snprintf(buf, cap, "%sInfinity", num < 0 ? "-" : "");
    }
    return (size_t)snprintf(buf, cap, "%.15g", num);
}

static PSString *ps_member_key(PSVM *vm, PSEnv *env, PSAstNode *member, PSEvalControl *ctl);

static PSString *ps_concat_string_number(PSVM *vm, PSString *str, double num, int number_first) {
    if (!str) return NULL;
    (void)vm;
    char num_buf[64];
    size_t num_len = ps_number_to_chars(num, num_buf, sizeof(num_buf));
    size_t total_len = str->byte_len + num_len;
    char *buf = (char *)malloc(total_len);
    if (!buf) return NULL;
    if (number_first) {
        memcpy(buf, num_buf, num_len);
        memcpy(buf + num_len, str->utf8, str->byte_len);
    } else {
        memcpy(buf, str->utf8, str->byte_len);
        memcpy(buf + str->byte_len, num_buf, num_len);
    }
    PSString *out = ps_string_from_utf8(buf, total_len);
    free(buf);
    return out;
}

static PSString *ps_member_key_read(PSVM *vm,
                                    PSEnv *env,
                                    PSAstNode *member,
                                    PSEvalControl *ctl,
                                    PSString *tmp,
                                    char *tmp_buf,
                                    size_t tmp_cap) {
    if (!member || member->kind != AST_MEMBER) return NULL;
    if (!member->as.member.computed) {
        return ps_identifier_string(member->as.member.property);
    }
    PSAstNode *prop = member->as.member.property;
    if (prop && prop->kind == AST_LITERAL) {
        PSValue lit = prop->as.literal.value;
        if (lit.type == PS_T_STRING) {
            return lit.as.string;
        }
        if (lit.type == PS_T_NUMBER) {
            double num = lit.as.number;
            if (num >= 0.0 && num <= (double)SIZE_MAX) {
                size_t idx = (size_t)num;
                if ((double)idx == num) {
                    return ps_array_index_string(vm, idx);
                }
            }
        }
    }
    if (prop && prop->kind == AST_BINARY && prop->as.binary.op == TOK_PLUS) {
        PSAstNode *left = prop->as.binary.left;
        PSAstNode *right = prop->as.binary.right;
        if (ps_literal_string_equals(left, "k", 1)) {
            PSValue right_val = eval_expression(vm, env, right, ctl);
            if (ctl->did_throw) return NULL;
            PSValue rprim = ps_to_primitive(vm, right_val, PS_HINT_NONE);
            if (ps_check_pending_throw(vm, ctl)) return NULL;
            if (rprim.type == PS_T_NUMBER) {
                char num_buf[64];
                size_t num_len = ps_number_to_chars(rprim.as.number, num_buf, sizeof(num_buf));
                if (1 + num_len <= tmp_cap && tmp && tmp_buf) {
                    tmp_buf[0] = 'k';
                    memcpy(tmp_buf + 1, num_buf, num_len);
                    tmp->utf8 = tmp_buf;
                    tmp->byte_len = 1 + num_len;
                    tmp->glyph_offsets = NULL;
                    tmp->glyph_count = 0;
                    tmp->index_state = 0;
                    tmp->index_value = 0;
                    uint32_t hash = 2166136261u;
                    for (size_t i = 0; i < tmp->byte_len; i++) {
                        hash ^= (uint8_t)tmp_buf[i];
                        hash *= 16777619u;
                    }
                    tmp->hash = hash;
                    return tmp;
                }
            }
        }
    }
    return ps_member_key(vm, env, member, ctl);
}

static PSString *ps_member_key(PSVM *vm, PSEnv *env, PSAstNode *member, PSEvalControl *ctl) {
    if (!member || member->kind != AST_MEMBER) return NULL;
    if (!member->as.member.computed) {
        return ps_identifier_string(member->as.member.property);
    }
    PSValue key_val = eval_expression(vm, env, member->as.member.property, ctl);
    if (ctl->did_throw) return NULL;
    if (key_val.type == PS_T_NUMBER) {
        double num = key_val.as.number;
        if (num >= 0.0 && num <= (double)SIZE_MAX) {
            size_t idx = (size_t)num;
            if ((double)idx == num) {
                return ps_array_index_string(vm, idx);
            }
        }
    }
    PSString *key = ps_to_string(vm, key_val);
    if (ps_check_pending_throw(vm, ctl)) return NULL;
    return key;
}

#define PS_FAST_FLAG_FIB 0x01
#define PS_FAST_FLAG_ENV 0x02
#define PS_FAST_FLAG_MATH 0x04
#define PS_FAST_FLAG_NUM 0x08
#define PS_FAST_FLAG_CLAMP 0x10
#define PS_FAST_FLAG_HOIST 0x20
#define PS_FAST_CHECKED_FIB 0x01
#define PS_FAST_CHECKED_ENV 0x02
#define PS_FAST_CHECKED_MATH 0x04
#define PS_FAST_CHECKED_NUM 0x08
#define PS_FAST_CHECKED_CLAMP 0x10
#define PS_FAST_CHECKED_HOIST 0x20

enum {
    PS_CALL_CACHE_NONE = 0,
    PS_CALL_CACHE_IDENT_FAST = 1,
    PS_CALL_CACHE_IDENT_PROP = 2,
    PS_CALL_CACHE_MEMBER = 3
};

typedef struct {
    PSString **items;
    size_t count;
    size_t cap;
} PSFastNameList;

typedef struct {
    PSFastNameList locals;
    int has_inner_func;
    int uses_eval;
    int uses_arguments;
    int uses_with;
} PSFastEnvScan;

static int ps_fast_name_list_add(PSFastNameList *list, PSString *name) {
    if (!list || !name) return 1;
    for (size_t i = 0; i < list->count; i++) {
        if (ps_string_equals(list->items[i], name)) return 1;
    }
    if (list->count == list->cap) {
        size_t new_cap = list->cap ? list->cap * 2 : 8;
        PSString **next = (PSString **)realloc(list->items, sizeof(PSString *) * new_cap);
        if (!next) return 0;
        list->items = next;
        list->cap = new_cap;
    }
    list->items[list->count++] = name;
    return 1;
}

static void ps_fast_env_scan_node(PSAstNode *node, PSFastEnvScan *scan);

static void ps_fast_env_scan_list(PSAstNode **items, size_t count, PSFastEnvScan *scan) {
    if (!items || !scan) return;
    for (size_t i = 0; i < count; i++) {
        ps_fast_env_scan_node(items[i], scan);
    }
}

static void ps_fast_env_scan_node(PSAstNode *node, PSFastEnvScan *scan) {
    if (!node || !scan) return;
    switch (node->kind) {
        case AST_PROGRAM:
        case AST_BLOCK:
            ps_fast_env_scan_list(node->as.list.items, node->as.list.count, scan);
            return;
        case AST_VAR_DECL: {
            PSString *name = ps_identifier_string(node->as.var_decl.id);
            (void)ps_fast_name_list_add(&scan->locals, name);
            if (node->as.var_decl.init) {
                ps_fast_env_scan_node(node->as.var_decl.init, scan);
            }
            return;
        }
        case AST_EXPR_STMT:
            ps_fast_env_scan_node(node->as.expr_stmt.expr, scan);
            return;
        case AST_RETURN:
            ps_fast_env_scan_node(node->as.ret.expr, scan);
            return;
        case AST_IF:
            ps_fast_env_scan_node(node->as.if_stmt.cond, scan);
            ps_fast_env_scan_node(node->as.if_stmt.then_branch, scan);
            ps_fast_env_scan_node(node->as.if_stmt.else_branch, scan);
            return;
        case AST_WHILE:
            ps_fast_env_scan_node(node->as.while_stmt.cond, scan);
            ps_fast_env_scan_node(node->as.while_stmt.body, scan);
            return;
        case AST_DO_WHILE:
            ps_fast_env_scan_node(node->as.do_while.body, scan);
            ps_fast_env_scan_node(node->as.do_while.cond, scan);
            return;
        case AST_FOR:
            ps_fast_env_scan_node(node->as.for_stmt.init, scan);
            ps_fast_env_scan_node(node->as.for_stmt.test, scan);
            ps_fast_env_scan_node(node->as.for_stmt.update, scan);
            ps_fast_env_scan_node(node->as.for_stmt.body, scan);
            return;
        case AST_FOR_IN:
            if (node->as.for_in.is_var &&
                node->as.for_in.target &&
                node->as.for_in.target->kind == AST_IDENTIFIER) {
                PSString *name = ps_identifier_string(node->as.for_in.target);
                (void)ps_fast_name_list_add(&scan->locals, name);
            }
            ps_fast_env_scan_node(node->as.for_in.target, scan);
            ps_fast_env_scan_node(node->as.for_in.object, scan);
            ps_fast_env_scan_node(node->as.for_in.body, scan);
            return;
        case AST_FOR_OF:
            if (node->as.for_of.is_var &&
                node->as.for_of.target &&
                node->as.for_of.target->kind == AST_IDENTIFIER) {
                PSString *name = ps_identifier_string(node->as.for_of.target);
                (void)ps_fast_name_list_add(&scan->locals, name);
            }
            ps_fast_env_scan_node(node->as.for_of.target, scan);
            ps_fast_env_scan_node(node->as.for_of.object, scan);
            ps_fast_env_scan_node(node->as.for_of.body, scan);
            return;
        case AST_SWITCH:
            ps_fast_env_scan_node(node->as.switch_stmt.expr, scan);
            ps_fast_env_scan_list(node->as.switch_stmt.cases,
                                  node->as.switch_stmt.case_count, scan);
            return;
        case AST_CASE:
            ps_fast_env_scan_node(node->as.case_stmt.test, scan);
            ps_fast_env_scan_list(node->as.case_stmt.items,
                                  node->as.case_stmt.count, scan);
            return;
        case AST_LABEL:
            ps_fast_env_scan_node(node->as.label_stmt.stmt, scan);
            return;
        case AST_WITH:
            scan->uses_with = 1;
            ps_fast_env_scan_node(node->as.with_stmt.object, scan);
            ps_fast_env_scan_node(node->as.with_stmt.body, scan);
            return;
        case AST_THROW:
            ps_fast_env_scan_node(node->as.throw_stmt.expr, scan);
            return;
        case AST_TRY:
            ps_fast_env_scan_node(node->as.try_stmt.try_block, scan);
            ps_fast_env_scan_node(node->as.try_stmt.catch_block, scan);
            ps_fast_env_scan_node(node->as.try_stmt.finally_block, scan);
            return;
        case AST_FUNCTION_DECL: {
            PSString *name = ps_identifier_string(node->as.func_decl.id);
            (void)ps_fast_name_list_add(&scan->locals, name);
            scan->has_inner_func = 1;
            return;
        }
        case AST_FUNCTION_EXPR:
            scan->has_inner_func = 1;
            return;
        case AST_IDENTIFIER: {
            PSString *name = ps_identifier_string(node);
            if (ps_string_equals_cstr(name, "arguments")) {
                scan->uses_arguments = 1;
            }
            return;
        }
        case AST_ASSIGN:
            ps_fast_env_scan_node(node->as.assign.target, scan);
            ps_fast_env_scan_node(node->as.assign.value, scan);
            return;
        case AST_BINARY:
            ps_fast_env_scan_node(node->as.binary.left, scan);
            ps_fast_env_scan_node(node->as.binary.right, scan);
            return;
        case AST_UNARY:
            ps_fast_env_scan_node(node->as.unary.expr, scan);
            return;
        case AST_UPDATE:
            ps_fast_env_scan_node(node->as.update.expr, scan);
            return;
        case AST_CONDITIONAL:
            ps_fast_env_scan_node(node->as.conditional.cond, scan);
            ps_fast_env_scan_node(node->as.conditional.then_expr, scan);
            ps_fast_env_scan_node(node->as.conditional.else_expr, scan);
            return;
        case AST_CALL: {
            PSAstNode *callee = node->as.call.callee;
            if (callee && callee->kind == AST_IDENTIFIER) {
                PSString *name = ps_identifier_string(callee);
                if (ps_string_equals_cstr(name, "eval")) {
                    scan->uses_eval = 1;
                }
            }
            ps_fast_env_scan_node(callee, scan);
            ps_fast_env_scan_list(node->as.call.args, node->as.call.argc, scan);
            return;
        }
        case AST_MEMBER:
            ps_fast_env_scan_node(node->as.member.object, scan);
            if (node->as.member.computed) {
                ps_fast_env_scan_node(node->as.member.property, scan);
            }
            return;
        case AST_NEW:
            ps_fast_env_scan_node(node->as.new_expr.callee, scan);
            ps_fast_env_scan_list(node->as.new_expr.args, node->as.new_expr.argc, scan);
            return;
        case AST_ARRAY_LITERAL:
            ps_fast_env_scan_list(node->as.array_literal.items,
                                  node->as.array_literal.count, scan);
            return;
        case AST_OBJECT_LITERAL: {
            for (size_t i = 0; i < node->as.object_literal.count; i++) {
                ps_fast_env_scan_node(node->as.object_literal.props[i].value, scan);
            }
            return;
        }
        case AST_THIS:
        case AST_LITERAL:
        case AST_BREAK:
        case AST_CONTINUE:
            return;
        default:
            return;
    }
}

static void ps_fast_env_prepare(PSFunction *func) {
    if (!func || func->is_native) return;
    if (func->fast_checked & PS_FAST_CHECKED_ENV) return;
    func->fast_checked |= PS_FAST_CHECKED_ENV;

    PSFastEnvScan scan = {0};
    ps_fast_env_scan_node(func->body, &scan);
    if (func->param_defaults) {
        for (size_t i = 0; i < func->param_count; i++) {
            if (func->param_defaults[i]) {
                ps_fast_env_scan_node(func->param_defaults[i], &scan);
            }
        }
    }

    int can_fast_env = !(scan.has_inner_func || scan.uses_eval || scan.uses_arguments || scan.uses_with);

    PSFastNameList names = {0};
    PSString *this_name = ps_string_from_cstr("this");
    if (!ps_fast_name_list_add(&names, this_name)) {
        free(scan.locals.items);
        free(names.items);
        return;
    }
    for (size_t i = 0; i < func->param_count; i++) {
        PSString *pname = func->param_names ? func->param_names[i] : NULL;
        if (!pname && func->params && func->params[i] && func->params[i]->kind == AST_IDENTIFIER) {
            pname = ps_identifier_string(func->params[i]);
        }
        if (pname) {
            if (!ps_fast_name_list_add(&names, pname)) {
                free(scan.locals.items);
                free(names.items);
                return;
            }
        }
    }
    size_t *local_index = NULL;
    size_t local_index_count = 0;
    if (scan.locals.count > 0) {
        local_index = (size_t *)calloc(scan.locals.count, sizeof(size_t));
        if (!local_index) {
            free(scan.locals.items);
            free(names.items);
            return;
        }
    }
    for (size_t i = 0; i < scan.locals.count; i++) {
        PSString *lname = scan.locals.items[i];
        size_t existing_idx = SIZE_MAX;
        for (size_t j = 0; j < names.count; j++) {
            if (ps_string_equals(names.items[j], lname)) {
                existing_idx = j;
                break;
            }
        }
        if (existing_idx != SIZE_MAX) {
            continue;
        }
        if (!ps_fast_name_list_add(&names, lname)) {
            free(scan.locals.items);
            free(names.items);
            free(local_index);
            return;
        }
        local_index[local_index_count++] = names.count - 1;
    }

    size_t *param_index = NULL;
    if (func->param_count > 0) {
        param_index = (size_t *)calloc(func->param_count, sizeof(size_t));
        if (!param_index) {
            free(scan.locals.items);
            free(names.items);
            return;
        }
        for (size_t i = 0; i < func->param_count; i++) {
            PSString *pname = func->param_names ? func->param_names[i] : NULL;
            if (!pname && func->params && func->params[i] && func->params[i]->kind == AST_IDENTIFIER) {
                pname = ps_identifier_string(func->params[i]);
            }
            size_t idx = SIZE_MAX;
            if (pname) {
                for (size_t j = 0; j < names.count; j++) {
                    if (ps_string_equals(names.items[j], pname)) {
                        idx = j;
                        break;
                    }
                }
            }
            param_index[i] = idx;
        }
    }

    func->fast_names = names.items;
    func->fast_count = names.count;
    func->fast_param_index = param_index;
    func->fast_local_count = scan.locals.count;
    func->fast_this_index = 0;
    func->fast_local_index = local_index;
    func->fast_local_index_count = local_index_count;
    if (can_fast_env) {
        func->fast_flags |= PS_FAST_FLAG_ENV;
    }

    free(scan.locals.items);
}

static int ps_hoist_scan_node(PSAstNode *node) {
    if (!node) return 0;
    switch (node->kind) {
        case AST_VAR_DECL:
            return 1;
        case AST_FUNCTION_DECL:
            return 1;
        case AST_PROGRAM:
        case AST_BLOCK:
            for (size_t i = 0; i < node->as.list.count; i++) {
                if (ps_hoist_scan_node(node->as.list.items[i])) return 1;
            }
            return 0;
        case AST_IF:
            return ps_hoist_scan_node(node->as.if_stmt.then_branch) ||
                   ps_hoist_scan_node(node->as.if_stmt.else_branch);
        case AST_WHILE:
            return ps_hoist_scan_node(node->as.while_stmt.body);
        case AST_DO_WHILE:
            return ps_hoist_scan_node(node->as.do_while.body);
        case AST_FOR:
            return ps_hoist_scan_node(node->as.for_stmt.init) ||
                   ps_hoist_scan_node(node->as.for_stmt.body);
        case AST_FOR_IN:
            if (node->as.for_in.is_var &&
                node->as.for_in.target &&
                node->as.for_in.target->kind == AST_IDENTIFIER) {
                return 1;
            }
            return ps_hoist_scan_node(node->as.for_in.body);
        case AST_FOR_OF:
            if (node->as.for_of.is_var &&
                node->as.for_of.target &&
                node->as.for_of.target->kind == AST_IDENTIFIER) {
                return 1;
            }
            return ps_hoist_scan_node(node->as.for_of.body);
        case AST_SWITCH:
            for (size_t i = 0; i < node->as.switch_stmt.case_count; i++) {
                if (ps_hoist_scan_node(node->as.switch_stmt.cases[i])) return 1;
            }
            return 0;
        case AST_CASE:
            for (size_t i = 0; i < node->as.case_stmt.count; i++) {
                if (ps_hoist_scan_node(node->as.case_stmt.items[i])) return 1;
            }
            return 0;
        case AST_WITH:
            return ps_hoist_scan_node(node->as.with_stmt.body);
        case AST_TRY:
            return ps_hoist_scan_node(node->as.try_stmt.try_block) ||
                   ps_hoist_scan_node(node->as.try_stmt.catch_block) ||
                   ps_hoist_scan_node(node->as.try_stmt.finally_block);
        case AST_LABEL:
            return ps_hoist_scan_node(node->as.label_stmt.stmt);
        default:
            return 0;
    }
}

static void ps_fast_hoist_prepare(PSFunction *func) {
    if (!func || func->is_native) return;
    if (func->fast_checked & PS_FAST_CHECKED_HOIST) return;
    func->fast_checked |= PS_FAST_CHECKED_HOIST;
    if (ps_hoist_scan_node(func->body)) {
        func->fast_flags |= PS_FAST_FLAG_HOIST;
    }
}

static int ps_fast_add_applicable(PSFunction *func, PSAstNode **left_id, PSAstNode **right_id) {
#if !PS_ENABLE_FAST_CALLS
    (void)func;
    (void)left_id;
    (void)right_id;
    return 0;
#else
    if (!func || func->is_native) return 0;
    if (func->param_count != 2) return 0;
    if (func->param_defaults && (func->param_defaults[0] || func->param_defaults[1])) return 0;
    if (!func->body || func->body->kind != AST_BLOCK) return 0;
    if (func->body->as.list.count != 1) return 0;
    PSAstNode *stmt = func->body->as.list.items[0];
    if (!stmt || stmt->kind != AST_RETURN || !stmt->as.ret.expr) return 0;
    PSAstNode *expr = stmt->as.ret.expr;
    if (expr->kind != AST_BINARY || expr->as.binary.op != TOK_PLUS) return 0;
    PSAstNode *left = expr->as.binary.left;
    PSAstNode *right = expr->as.binary.right;
    if (!left || !right) return 0;
    if (left->kind != AST_IDENTIFIER || right->kind != AST_IDENTIFIER) return 0;
    if (!func->param_names || !func->param_names[0] || !func->param_names[1]) return 0;
    PSString *left_name = ps_identifier_string(left);
    PSString *right_name = ps_identifier_string(right);
    if (!ps_string_equals(left_name, func->param_names[0])) return 0;
    if (!ps_string_equals(right_name, func->param_names[1])) return 0;
    if (left_id) *left_id = left;
    if (right_id) *right_id = right;
    return 1;
#endif
}

static int ps_literal_number_equals(PSAstNode *node, double expected) {
    if (!node || node->kind != AST_LITERAL) return 0;
    if (node->as.literal.value.type != PS_T_NUMBER) return 0;
    return node->as.literal.value.as.number == expected;
}

static int ps_match_identifier(PSAstNode *node, PSString *name) {
    if (!node || node->kind != AST_IDENTIFIER || !name) return 0;
    return ps_string_equals(ps_identifier_string(node), name);
}

static int ps_match_fib_call(PSAstNode *node, PSString *fn_name, PSString *param_name, double sub) {
    if (!node || node->kind != AST_CALL) return 0;
    if (node->as.call.argc != 1 || !node->as.call.args) return 0;
    PSAstNode *callee = node->as.call.callee;
    if (!callee || callee->kind != AST_IDENTIFIER) return 0;
    if (!ps_string_equals(ps_identifier_string(callee), fn_name)) return 0;
    PSAstNode *arg = node->as.call.args[0];
    if (!arg || arg->kind != AST_BINARY || arg->as.binary.op != TOK_MINUS) return 0;
    if (!ps_match_identifier(arg->as.binary.left, param_name)) return 0;
    if (!ps_literal_number_equals(arg->as.binary.right, sub)) return 0;
    return 1;
}

static int ps_fast_fib_applicable(PSFunction *func) {
#if !PS_ENABLE_FAST_CALLS
    (void)func;
    return 0;
#else
    if (!func || func->is_native) return 0;
    if (func->fast_checked & PS_FAST_CHECKED_FIB) {
        return (func->fast_flags & PS_FAST_FLAG_FIB) != 0;
    }
    func->fast_checked |= PS_FAST_CHECKED_FIB;
    if (func->param_count != 1) return 0;
    if (func->param_defaults && func->param_defaults[0]) return 0;
    if (!func->body || func->body->kind != AST_BLOCK) return 0;
    if (func->body->as.list.count != 2) return 0;
    if (!func->name) return 0;
    PSString *param_name = NULL;
    if (func->param_names && func->param_names[0]) {
        param_name = func->param_names[0];
    } else if (func->params && func->params[0] && func->params[0]->kind == AST_IDENTIFIER) {
        param_name = ps_identifier_string(func->params[0]);
    }
    if (!param_name) return 0;

    PSAstNode *if_stmt = func->body->as.list.items[0];
    PSAstNode *ret_stmt = func->body->as.list.items[1];
    if (!if_stmt || if_stmt->kind != AST_IF) return 0;
    if (!ret_stmt || ret_stmt->kind != AST_RETURN || !ret_stmt->as.ret.expr) return 0;

    PSAstNode *cond = if_stmt->as.if_stmt.cond;
    if (!cond || cond->kind != AST_BINARY || cond->as.binary.op != TOK_LT) return 0;
    if (!ps_match_identifier(cond->as.binary.left, param_name)) return 0;
    if (!ps_literal_number_equals(cond->as.binary.right, 2.0)) return 0;

    PSAstNode *then_branch = if_stmt->as.if_stmt.then_branch;
    if (!then_branch || then_branch->kind != AST_RETURN || !then_branch->as.ret.expr) return 0;
    if (if_stmt->as.if_stmt.else_branch) return 0;
    if (!ps_match_identifier(then_branch->as.ret.expr, param_name)) return 0;

    PSAstNode *sum = ret_stmt->as.ret.expr;
    if (!sum || sum->kind != AST_BINARY || sum->as.binary.op != TOK_PLUS) return 0;
    if (!ps_match_fib_call(sum->as.binary.left, func->name, param_name, 1.0)) return 0;
    if (!ps_match_fib_call(sum->as.binary.right, func->name, param_name, 2.0)) return 0;

    func->fast_flags |= PS_FAST_FLAG_FIB;
    return 1;
#endif
}

static double ps_fast_fib_value(uint64_t n) {
    double a = 0.0;
    double b = 1.0;
    for (uint64_t i = 0; i < n; i++) {
        double next = a + b;
        a = b;
        b = next;
    }
    return a;
}

static int ps_fast_math_param_index(PSFunction *func, PSString *name, size_t *out_index) {
    if (!func || !name || !func->param_names) return 0;
    for (size_t i = 0; i < func->param_count; i++) {
        if (func->param_names[i] && ps_string_equals(func->param_names[i], name)) {
            if (out_index) *out_index = i;
            return 1;
        }
    }
    return 0;
}

static int ps_fast_math_expr_ok(PSFunction *func, PSAstNode *expr) {
    if (!expr || !func) return 0;
    switch (expr->kind) {
        case AST_LITERAL:
            return expr->as.literal.value.type == PS_T_NUMBER;
        case AST_IDENTIFIER:
            return ps_fast_math_param_index(func, ps_identifier_string(expr), NULL);
        case AST_UNARY:
            if (expr->as.unary.op != TOK_PLUS && expr->as.unary.op != TOK_MINUS) return 0;
            return ps_fast_math_expr_ok(func, expr->as.unary.expr);
        case AST_BINARY: {
            int op = expr->as.binary.op;
            if (op != TOK_PLUS && op != TOK_MINUS && op != TOK_STAR &&
                op != TOK_SLASH && op != TOK_PERCENT) {
                return 0;
            }
            return ps_fast_math_expr_ok(func, expr->as.binary.left) &&
                ps_fast_math_expr_ok(func, expr->as.binary.right);
        }
        default:
            return 0;
    }
}

static int ps_match_identifier_node(PSAstNode *node, PSString *name) {
    if (!node || node->kind != AST_IDENTIFIER || !name) return 0;
    return ps_string_equals(ps_identifier_string(node), name);
}

static int ps_match_math_floor_call(PSAstNode *expr, PSString *param_name) {
    if (!expr || expr->kind != AST_CALL) return 0;
    PSAstNode *callee = expr->as.call.callee;
    if (!callee || callee->kind != AST_MEMBER) return 0;
    PSAstNode *member = callee;
    if (member->as.member.computed) return 0;
    PSAstNode *obj = member->as.member.object;
    PSAstNode *prop = member->as.member.property;
    if (!obj || obj->kind != AST_IDENTIFIER) return 0;
    PSString *obj_name = ps_identifier_string(obj);
    if (!ps_string_equals_cstr(obj_name, "Math")) return 0;
    if (!prop || prop->kind != AST_IDENTIFIER) return 0;
    PSString *fn = ps_identifier_string(prop);
    if (!ps_string_equals_cstr(fn, "floor")) return 0;
    if (expr->as.call.argc != 1) return 0;
    return ps_match_identifier_node(expr->as.call.args[0], param_name);
}

static void ps_fast_math_prepare(PSFunction *func) {
    if (!func || func->is_native) return;
    if (func->fast_checked & PS_FAST_CHECKED_MATH) return;
    func->fast_checked |= PS_FAST_CHECKED_MATH;
    if (!func->body || func->body->kind != AST_BLOCK) return;
    if (func->body->as.list.count != 1) return;
    PSAstNode *stmt = func->body->as.list.items[0];
    if (!stmt || stmt->kind != AST_RETURN || !stmt->as.ret.expr) return;
    if (!ps_fast_math_expr_ok(func, stmt->as.ret.expr)) return;
    func->fast_flags |= PS_FAST_FLAG_MATH;
    func->fast_math_expr = stmt->as.ret.expr;
}

static int ps_fast_math_eval(PSFunction *func,
                             PSAstNode *expr,
                             int argc,
                             PSValue *argv,
                             double *out) {
    if (!expr || !func) return 0;
    switch (expr->kind) {
        case AST_LITERAL:
            if (expr->as.literal.value.type != PS_T_NUMBER) return 0;
            if (out) *out = expr->as.literal.value.as.number;
            return 1;
        case AST_IDENTIFIER: {
            size_t idx = 0;
            if (!ps_fast_math_param_index(func, ps_identifier_string(expr), &idx)) return 0;
            if ((int)idx >= argc || !argv) return 0;
            if (argv[idx].type != PS_T_NUMBER) return 0;
            if (out) *out = argv[idx].as.number;
            return 1;
        }
        case AST_UNARY: {
            double v = 0.0;
            if (!ps_fast_math_eval(func, expr->as.unary.expr, argc, argv, &v)) return 0;
            if (expr->as.unary.op == TOK_MINUS) v = -v;
            if (out) *out = v;
            return 1;
        }
        case AST_BINARY: {
            double l = 0.0;
            double r = 0.0;
            if (!ps_fast_math_eval(func, expr->as.binary.left, argc, argv, &l)) return 0;
            if (!ps_fast_math_eval(func, expr->as.binary.right, argc, argv, &r)) return 0;
            switch (expr->as.binary.op) {
                case TOK_PLUS: l = l + r; break;
                case TOK_MINUS: l = l - r; break;
                case TOK_STAR: l = l * r; break;
                case TOK_SLASH: l = l / r; break;
                case TOK_PERCENT: l = fmod(l, r); break;
                default: return 0;
            }
            if (out) *out = l;
            return 1;
        }
        default:
            return 0;
    }
}

typedef struct {
    PSVM *vm;
    PSFunction *func;
    int argc;
    PSValue *argv;
    PSString **local_names;
    double *local_vals;
    size_t local_count;
} PSFastNumCtx;

typedef struct PSFastNumOp {
    uint8_t op;
    uint8_t kind;
    uint16_t pad;
    uint32_t index;
    double imm;
    PSAstNode *node;
} PSFastNumOp;

typedef struct {
    PSFastNumOp *items;
    size_t count;
    size_t cap;
} PSFastNumOpList;

#define PS_FAST_NUM_OP_CONST 1
#define PS_FAST_NUM_OP_PARAM 2
#define PS_FAST_NUM_OP_LOCAL 3
#define PS_FAST_NUM_OP_ENV 4
#define PS_FAST_NUM_OP_STORE_LOCAL 5
#define PS_FAST_NUM_OP_MEMBER 6
#define PS_FAST_NUM_OP_NEG 7
#define PS_FAST_NUM_OP_ADD 8
#define PS_FAST_NUM_OP_SUB 9
#define PS_FAST_NUM_OP_MUL 10
#define PS_FAST_NUM_OP_DIV 11
#define PS_FAST_NUM_OP_MOD 12
#define PS_FAST_NUM_OP_MATH_SQRT 13
#define PS_FAST_NUM_OP_MATH_ABS 14
#define PS_FAST_NUM_OP_MATH_POW 15
#define PS_FAST_NUM_OP_MATH_FLOOR 16
#define PS_FAST_NUM_OP_MATH_CEIL 17
#define PS_FAST_NUM_OP_MATH_ROUND 18
#define PS_FAST_NUM_OP_MATH_MIN 19
#define PS_FAST_NUM_OP_MATH_MAX 20
#define PS_FAST_NUM_OP_RETURN 21

#define PS_FAST_NUM_KIND_NONE 0
#define PS_FAST_NUM_KIND_PARAM 1
#define PS_FAST_NUM_KIND_LOCAL 2
#define PS_FAST_NUM_KIND_ENV 3

#define PS_FAST_NUM_MATH_NONE 0
#define PS_FAST_NUM_MATH_SQRT 1
#define PS_FAST_NUM_MATH_ABS 2
#define PS_FAST_NUM_MATH_MIN 3
#define PS_FAST_NUM_MATH_MAX 4
#define PS_FAST_NUM_MATH_POW 5
#define PS_FAST_NUM_MATH_FLOOR 6
#define PS_FAST_NUM_MATH_CEIL 7
#define PS_FAST_NUM_MATH_ROUND 8

static uint8_t ps_fast_num_math_id(PSAstNode *expr) {
    if (!expr || expr->kind != AST_CALL) return PS_FAST_NUM_MATH_NONE;
    PSAstNode *callee = expr->as.call.callee;
    if (!callee || callee->kind != AST_MEMBER) return PS_FAST_NUM_MATH_NONE;
    PSAstNode *member = callee;
    if (member->as.member.computed) return PS_FAST_NUM_MATH_NONE;
    PSAstNode *obj = member->as.member.object;
    PSAstNode *prop = member->as.member.property;
    if (!obj || obj->kind != AST_IDENTIFIER) return PS_FAST_NUM_MATH_NONE;
    if (!prop || prop->kind != AST_IDENTIFIER) return PS_FAST_NUM_MATH_NONE;
    PSString *obj_name = ps_identifier_string(obj);
    if (!ps_string_equals_cstr(obj_name, "Math")) return PS_FAST_NUM_MATH_NONE;
    PSString *fn = ps_identifier_string(prop);
    if (!fn) return PS_FAST_NUM_MATH_NONE;
    if (ps_string_equals_cstr(fn, "sqrt")) return PS_FAST_NUM_MATH_SQRT;
    if (ps_string_equals_cstr(fn, "abs")) return PS_FAST_NUM_MATH_ABS;
    if (ps_string_equals_cstr(fn, "min")) return PS_FAST_NUM_MATH_MIN;
    if (ps_string_equals_cstr(fn, "max")) return PS_FAST_NUM_MATH_MAX;
    if (ps_string_equals_cstr(fn, "pow")) return PS_FAST_NUM_MATH_POW;
    if (ps_string_equals_cstr(fn, "floor")) return PS_FAST_NUM_MATH_FLOOR;
    if (ps_string_equals_cstr(fn, "ceil")) return PS_FAST_NUM_MATH_CEIL;
    if (ps_string_equals_cstr(fn, "round")) return PS_FAST_NUM_MATH_ROUND;
    return PS_FAST_NUM_MATH_NONE;
}

static int ps_fast_num_op_push(PSFastNumOpList *list, PSFastNumOp op) {
    if (!list) return 0;
    if (list->count >= list->cap) {
        size_t new_cap = list->cap ? list->cap * 2 : 32;
        PSFastNumOp *items = (PSFastNumOp *)realloc(list->items, new_cap * sizeof(PSFastNumOp));
        if (!items) return 0;
        list->items = items;
        list->cap = new_cap;
    }
    list->items[list->count++] = op;
    return 1;
}

static int ps_fast_num_emit_expr(PSFunction *func, PSAstNode *expr, PSFastNumOpList *out) {
    if (!func || !expr || !out) return 0;
    switch (expr->kind) {
        case AST_LITERAL: {
            if (expr->as.literal.value.type != PS_T_NUMBER) return 0;
            PSFastNumOp op = {0};
            op.op = PS_FAST_NUM_OP_CONST;
            op.imm = expr->as.literal.value.as.number;
            return ps_fast_num_op_push(out, op);
        }
        case AST_IDENTIFIER: {
            uint8_t kind = expr->as.identifier.fast_num_kind;
            uint32_t idx = expr->as.identifier.fast_num_index;
            PSFastNumOp op = {0};
            if (kind == PS_FAST_NUM_KIND_PARAM) {
                op.op = PS_FAST_NUM_OP_PARAM;
                op.index = idx;
                return ps_fast_num_op_push(out, op);
            }
            if (kind == PS_FAST_NUM_KIND_LOCAL) {
                op.op = PS_FAST_NUM_OP_LOCAL;
                op.index = idx;
                return ps_fast_num_op_push(out, op);
            }
            if (kind == PS_FAST_NUM_KIND_ENV) {
                op.op = PS_FAST_NUM_OP_ENV;
                op.node = expr;
                return ps_fast_num_op_push(out, op);
            }
            return 0;
        }
        case AST_UNARY: {
            if (!ps_fast_num_emit_expr(func, expr->as.unary.expr, out)) return 0;
            if (expr->as.unary.op == TOK_MINUS) {
                PSFastNumOp op = {0};
                op.op = PS_FAST_NUM_OP_NEG;
                return ps_fast_num_op_push(out, op);
            }
            return 1;
        }
        case AST_BINARY: {
            int op = expr->as.binary.op;
            if (!ps_fast_num_emit_expr(func, expr->as.binary.left, out)) return 0;
            if (!ps_fast_num_emit_expr(func, expr->as.binary.right, out)) return 0;
            PSFastNumOp inst = {0};
            switch (op) {
                case TOK_PLUS: inst.op = PS_FAST_NUM_OP_ADD; break;
                case TOK_MINUS: inst.op = PS_FAST_NUM_OP_SUB; break;
                case TOK_STAR: inst.op = PS_FAST_NUM_OP_MUL; break;
                case TOK_SLASH: inst.op = PS_FAST_NUM_OP_DIV; break;
                case TOK_PERCENT: inst.op = PS_FAST_NUM_OP_MOD; break;
                default: return 0;
            }
            return ps_fast_num_op_push(out, inst);
        }
        case AST_MEMBER: {
            PSAstNode *obj_node = expr->as.member.object;
            PSAstNode *prop = expr->as.member.property;
            if (!expr->as.member.computed) return 0;
            if (!obj_node || obj_node->kind != AST_IDENTIFIER) return 0;
            if (!prop || prop->kind != AST_LITERAL || prop->as.literal.value.type != PS_T_NUMBER) return 0;
            double num = prop->as.literal.value.as.number;
            if (num < 0.0 || num > (double)UINT32_MAX) return 0;
            uint32_t index = (uint32_t)num;
            if ((double)index != num) return 0;
            PSFastNumOp op = {0};
            op.op = PS_FAST_NUM_OP_MEMBER;
            op.index = index;
            op.node = obj_node;
            return ps_fast_num_op_push(out, op);
        }
        case AST_CALL: {
            uint8_t math_id = expr->as.call.fast_num_math_id;
            if (math_id == PS_FAST_NUM_MATH_NONE) {
                math_id = ps_fast_num_math_id(expr);
            }
            size_t argc = expr->as.call.argc;
            PSAstNode **args = expr->as.call.args;
            if (math_id == PS_FAST_NUM_MATH_MIN || math_id == PS_FAST_NUM_MATH_MAX) {
                for (size_t i = 0; i < argc; i++) {
                    if (!ps_fast_num_emit_expr(func, args[i], out)) return 0;
                }
                PSFastNumOp op = {0};
                op.op = (math_id == PS_FAST_NUM_MATH_MIN) ? PS_FAST_NUM_OP_MATH_MIN
                                                          : PS_FAST_NUM_OP_MATH_MAX;
                op.index = (uint32_t)argc;
                return ps_fast_num_op_push(out, op);
            }
            if (math_id == PS_FAST_NUM_MATH_POW) {
                if (argc > 0) {
                    if (!ps_fast_num_emit_expr(func, args[0], out)) return 0;
                } else {
                    PSFastNumOp zero = {0};
                    zero.op = PS_FAST_NUM_OP_CONST;
                    zero.imm = 0.0;
                    if (!ps_fast_num_op_push(out, zero)) return 0;
                }
                if (argc > 1) {
                    if (!ps_fast_num_emit_expr(func, args[1], out)) return 0;
                } else {
                    PSFastNumOp zero = {0};
                    zero.op = PS_FAST_NUM_OP_CONST;
                    zero.imm = 0.0;
                    if (!ps_fast_num_op_push(out, zero)) return 0;
                }
                PSFastNumOp op = {0};
                op.op = PS_FAST_NUM_OP_MATH_POW;
                return ps_fast_num_op_push(out, op);
            }
            if (math_id == PS_FAST_NUM_MATH_SQRT || math_id == PS_FAST_NUM_MATH_ABS ||
                math_id == PS_FAST_NUM_MATH_FLOOR || math_id == PS_FAST_NUM_MATH_CEIL ||
                math_id == PS_FAST_NUM_MATH_ROUND) {
                if (argc > 0) {
                    if (!ps_fast_num_emit_expr(func, args[0], out)) return 0;
                } else {
                    PSFastNumOp zero = {0};
                    zero.op = PS_FAST_NUM_OP_CONST;
                    zero.imm = 0.0;
                    if (!ps_fast_num_op_push(out, zero)) return 0;
                }
                PSFastNumOp op = {0};
                if (math_id == PS_FAST_NUM_MATH_SQRT) op.op = PS_FAST_NUM_OP_MATH_SQRT;
                else if (math_id == PS_FAST_NUM_MATH_ABS) op.op = PS_FAST_NUM_OP_MATH_ABS;
                else if (math_id == PS_FAST_NUM_MATH_FLOOR) op.op = PS_FAST_NUM_OP_MATH_FLOOR;
                else if (math_id == PS_FAST_NUM_MATH_CEIL) op.op = PS_FAST_NUM_OP_MATH_CEIL;
                else op.op = PS_FAST_NUM_OP_MATH_ROUND;
                return ps_fast_num_op_push(out, op);
            }
            return 0;
        }
        default:
            return 0;
    }
}

static int ps_fast_num_compile_ops(PSFunction *func,
                                   PSAstNode **inits,
                                   size_t init_count,
                                   PSAstNode *ret_expr) {
    if (!func || !ret_expr) return 0;
    PSFastNumOpList list = {0};
    for (size_t i = 0; i < init_count; i++) {
        if (!ps_fast_num_emit_expr(func, inits[i], &list)) {
            free(list.items);
            return 0;
        }
        PSFastNumOp store = {0};
        store.op = PS_FAST_NUM_OP_STORE_LOCAL;
        store.index = (uint32_t)i;
        if (!ps_fast_num_op_push(&list, store)) {
            free(list.items);
            return 0;
        }
    }
    if (!ps_fast_num_emit_expr(func, ret_expr, &list)) {
        free(list.items);
        return 0;
    }
    PSFastNumOp ret = {0};
    ret.op = PS_FAST_NUM_OP_RETURN;
    if (!ps_fast_num_op_push(&list, ret)) {
        free(list.items);
        return 0;
    }
    func->fast_num_ops = list.items;
    func->fast_num_ops_count = list.count;
    return 1;
}

static void ps_fast_num_bind_identifier(PSFunction *func, PSAstNode *expr) {
    if (!func || !expr || expr->kind != AST_IDENTIFIER) return;
    if (expr->as.identifier.fast_num_kind != PS_FAST_NUM_KIND_NONE) return;
    PSString *name = ps_identifier_string(expr);
    if (!name) return;
    if (func->param_names) {
        for (size_t i = 0; i < func->param_count; i++) {
            if (func->param_names[i] &&
                ps_string_equals(func->param_names[i], name)) {
                expr->as.identifier.fast_num_kind = PS_FAST_NUM_KIND_PARAM;
                expr->as.identifier.fast_num_index = (uint32_t)i;
                return;
            }
        }
    }
    if (func->fast_num_names) {
        for (size_t i = 0; i < func->fast_num_count; i++) {
            if (func->fast_num_names[i] &&
                ps_string_equals(func->fast_num_names[i], name)) {
                expr->as.identifier.fast_num_kind = PS_FAST_NUM_KIND_LOCAL;
                expr->as.identifier.fast_num_index = (uint32_t)i;
                return;
            }
        }
    }
    expr->as.identifier.fast_num_kind = PS_FAST_NUM_KIND_ENV;
    expr->as.identifier.fast_num_index = 0;
}

static void ps_fast_num_bind_expr(PSFunction *func, PSAstNode *expr) {
    if (!func || !expr) return;
    switch (expr->kind) {
        case AST_IDENTIFIER:
            ps_fast_num_bind_identifier(func, expr);
            return;
        case AST_UNARY:
            ps_fast_num_bind_expr(func, expr->as.unary.expr);
            return;
        case AST_BINARY:
            ps_fast_num_bind_expr(func, expr->as.binary.left);
            ps_fast_num_bind_expr(func, expr->as.binary.right);
            return;
        case AST_CONDITIONAL:
            ps_fast_num_bind_expr(func, expr->as.conditional.cond);
            ps_fast_num_bind_expr(func, expr->as.conditional.then_expr);
            ps_fast_num_bind_expr(func, expr->as.conditional.else_expr);
            return;
        case AST_MEMBER:
            ps_fast_num_bind_expr(func, expr->as.member.object);
            if (expr->as.member.computed) {
                ps_fast_num_bind_expr(func, expr->as.member.property);
            }
            return;
        case AST_CALL:
            ps_fast_num_bind_expr(func, expr->as.call.callee);
            for (size_t i = 0; i < expr->as.call.argc; i++) {
                ps_fast_num_bind_expr(func, expr->as.call.args[i]);
            }
            if (expr->as.call.fast_num_math_id == PS_FAST_NUM_MATH_NONE) {
                expr->as.call.fast_num_math_id = ps_fast_num_math_id(expr);
            }
            return;
        case AST_LITERAL:
        default:
            return;
    }
}

static int ps_fast_num_cond_ok(PSFunction *func, PSAstNode *expr);

static int ps_fast_num_expr_ok(PSFunction *func, PSAstNode *expr) {
    if (!expr || !func) return 0;
    switch (expr->kind) {
        case AST_LITERAL:
            return expr->as.literal.value.type == PS_T_NUMBER;
        case AST_IDENTIFIER:
            return 1;
        case AST_UNARY:
            if (expr->as.unary.op != TOK_PLUS && expr->as.unary.op != TOK_MINUS) return 0;
            return ps_fast_num_expr_ok(func, expr->as.unary.expr);
        case AST_BINARY: {
            int op = expr->as.binary.op;
            if (op != TOK_PLUS && op != TOK_MINUS && op != TOK_STAR &&
                op != TOK_SLASH && op != TOK_PERCENT) {
                return 0;
            }
            return ps_fast_num_expr_ok(func, expr->as.binary.left) &&
                ps_fast_num_expr_ok(func, expr->as.binary.right);
        }
        case AST_CONDITIONAL:
            if (!ps_fast_num_cond_ok(func, expr->as.conditional.cond)) return 0;
            return ps_fast_num_expr_ok(func, expr->as.conditional.then_expr) &&
                ps_fast_num_expr_ok(func, expr->as.conditional.else_expr);
        case AST_MEMBER: {
            if (!expr->as.member.computed) return 0;
            PSAstNode *obj = expr->as.member.object;
            PSAstNode *prop = expr->as.member.property;
            if (!obj || obj->kind != AST_IDENTIFIER) return 0;
            if (!prop) return 0;
            if (prop->kind == AST_LITERAL) {
                return prop->as.literal.value.type == PS_T_NUMBER ||
                    prop->as.literal.value.type == PS_T_STRING;
            }
            if (prop->kind == AST_IDENTIFIER) return 1;
            return 0;
        }
        case AST_CALL: {
            PSAstNode *callee = expr->as.call.callee;
            if (!callee || callee->kind != AST_MEMBER) return 0;
            PSAstNode *member = callee;
            if (member->as.member.computed) return 0;
            PSAstNode *obj = member->as.member.object;
            PSAstNode *prop = member->as.member.property;
            if (!obj || obj->kind != AST_IDENTIFIER) return 0;
            PSString *obj_name = ps_identifier_string(obj);
            if (!ps_string_equals_cstr(obj_name, "Math")) return 0;
            if (!prop || prop->kind != AST_IDENTIFIER) return 0;
            PSString *fn = ps_identifier_string(prop);
            if (!fn) return 0;
            if (!ps_string_equals_cstr(fn, "sqrt") &&
                !ps_string_equals_cstr(fn, "abs") &&
                !ps_string_equals_cstr(fn, "min") &&
                !ps_string_equals_cstr(fn, "max") &&
                !ps_string_equals_cstr(fn, "pow") &&
                !ps_string_equals_cstr(fn, "floor") &&
                !ps_string_equals_cstr(fn, "ceil") &&
                !ps_string_equals_cstr(fn, "round")) {
                return 0;
            }
            for (size_t i = 0; i < expr->as.call.argc; i++) {
                if (!ps_fast_num_expr_ok(func, expr->as.call.args[i])) return 0;
            }
            return 1;
        }
        default:
            return 0;
    }
}

static int ps_fast_num_cond_ok(PSFunction *func, PSAstNode *expr) {
    if (!expr || !func) return 0;
    if (expr->kind == AST_CONDITIONAL) return 0;
    if (ps_fast_num_expr_ok(func, expr)) return 1;
    if (expr->kind == AST_LITERAL) {
        return expr->as.literal.value.type == PS_T_BOOLEAN;
    }
    if (expr->kind == AST_UNARY && expr->as.unary.op == TOK_NOT) {
        return ps_fast_num_cond_ok(func, expr->as.unary.expr);
    }
    if (expr->kind == AST_BINARY) {
        int op = expr->as.binary.op;
        if (op == TOK_AND_AND || op == TOK_OR_OR) {
            return ps_fast_num_cond_ok(func, expr->as.binary.left) &&
                ps_fast_num_cond_ok(func, expr->as.binary.right);
        }
        if (op == TOK_LT || op == TOK_LTE || op == TOK_GT || op == TOK_GTE ||
            op == TOK_EQ || op == TOK_NEQ || op == TOK_STRICT_EQ || op == TOK_STRICT_NEQ) {
            return ps_fast_num_expr_ok(func, expr->as.binary.left) &&
                ps_fast_num_expr_ok(func, expr->as.binary.right);
        }
    }
    return 0;
}

static int ps_fast_num_lookup(PSFastNumCtx *ctx, PSString *name, double *out) {
    if (!ctx || !name || !out) return 0;
    if (ctx->func && ctx->func->param_names) {
        for (size_t i = 0; i < ctx->func->param_count; i++) {
            if (ctx->func->param_names[i] &&
                ps_string_equals(ctx->func->param_names[i], name)) {
                if (i >= (size_t)ctx->argc) return 0;
                if (!ctx->argv || ctx->argv[i].type != PS_T_NUMBER) return 0;
                *out = ctx->argv[i].as.number;
                return 1;
            }
        }
    }
    for (size_t i = 0; i < ctx->local_count; i++) {
        if (ctx->local_names[i] &&
            ps_string_equals(ctx->local_names[i], name)) {
            *out = ctx->local_vals[i];
            return 1;
        }
    }
    if (ctx->func && ctx->func->env) {
        int found = 0;
        PSValue v = ps_env_get(ctx->func->env, name, &found);
        if (found && v.type == PS_T_NUMBER) {
            *out = v.as.number;
            return 1;
        }
    }
    return 0;
}

static int ps_fast_num_identifier_value(PSFastNumCtx *ctx, PSAstNode *node, PSValue *out) {
    if (!ctx || !node || node->kind != AST_IDENTIFIER || !out) return 0;
    uint8_t kind = node->as.identifier.fast_num_kind;
    if (kind == PS_FAST_NUM_KIND_PARAM) {
        size_t idx = (size_t)node->as.identifier.fast_num_index;
        if (idx >= (size_t)ctx->argc) return 0;
        if (!ctx->argv) return 0;
        *out = ctx->argv[idx];
        return 1;
    }
    if (kind == PS_FAST_NUM_KIND_LOCAL) {
        size_t idx = (size_t)node->as.identifier.fast_num_index;
        if (idx >= ctx->local_count) return 0;
        *out = ps_value_number(ctx->local_vals[idx]);
        return 1;
    }
    if (kind == PS_FAST_NUM_KIND_ENV) {
        if (ctx->func && ctx->func->env) {
            int found = 0;
            PSValue v = ps_value_undefined();
            if (!ps_identifier_cached_get(ctx->func->env, node, &v, &found)) {
                PSString *name = ps_identifier_string(node);
                if (!name) return 0;
                v = ps_env_get(ctx->func->env, name, &found);
            }
            if (found) {
                *out = v;
                return 1;
            }
        }
        return 0;
    }
    PSString *name = ps_identifier_string(node);
    if (!name) return 0;
    if (ctx->func && ctx->func->param_names) {
        for (size_t i = 0; i < ctx->func->param_count; i++) {
            if (ctx->func->param_names[i] &&
                ps_string_equals(ctx->func->param_names[i], name)) {
                if (i >= (size_t)ctx->argc) return 0;
                if (!ctx->argv) return 0;
                *out = ctx->argv[i];
                return 1;
            }
        }
    }
    if (ctx->func && ctx->func->env) {
        int found = 0;
        PSValue v = ps_env_get(ctx->func->env, name, &found);
        if (found) {
            *out = v;
            return 1;
        }
    }
    return 0;
}

static int ps_fast_num_eval_expr(PSFastNumCtx *ctx, PSAstNode *expr, double *out);
static int ps_fast_num_eval_cond(PSFastNumCtx *ctx, PSAstNode *expr, int *out);

static int ps_fast_num_eval_member(PSFastNumCtx *ctx, PSAstNode *expr, double *out) {
    if (!ctx || !expr || expr->kind != AST_MEMBER || !out) return 0;
    if (!expr->as.member.computed) return 0;
    PSAstNode *obj_node = expr->as.member.object;
    PSAstNode *prop = expr->as.member.property;
    if (!obj_node || obj_node->kind != AST_IDENTIFIER || !prop) return 0;

    PSValue obj_val = ps_value_undefined();
    if (!ps_fast_num_identifier_value(ctx, obj_node, &obj_val)) return 0;
    if (obj_val.type != PS_T_OBJECT || !obj_val.as.object) return 0;
    PSObject *obj = obj_val.as.object;

    PSValue key_val = ps_value_undefined();
    if (prop->kind == AST_LITERAL) {
        key_val = prop->as.literal.value;
    } else if (prop->kind == AST_IDENTIFIER) {
        if (!ps_fast_num_identifier_value(ctx, prop, &key_val)) return 0;
    } else {
        return 0;
    }

    if (obj->kind == PS_OBJ_KIND_ARRAY || obj->kind == PS_OBJ_KIND_BUFFER) {
        size_t index = 0;
        if (key_val.type == PS_T_NUMBER) {
            double num = key_val.as.number;
            if (num < 0.0 || num > (double)SIZE_MAX) return 0;
            index = (size_t)num;
            if ((double)index != num) return 0;
        } else if (key_val.type == PS_T_STRING) {
            if (!ps_array_string_to_index(key_val.as.string, &index)) return 0;
        } else {
            return 0;
        }
        if (obj->kind == PS_OBJ_KIND_ARRAY) {
            PSValue elem = ps_value_undefined();
            if (!ps_array_get_index(obj, index, &elem)) return 0;
            if (elem.type != PS_T_NUMBER) return 0;
            *out = elem.as.number;
            return 1;
        }
        PSBuffer *buf = ps_buffer_from_object(obj);
        if (!buf || index >= buf->size) return 0;
        *out = (double)buf->data[index];
        return 1;
    }

    if (obj->kind == PS_OBJ_KIND_PLAIN) {
        if (key_val.type == PS_T_STRING) {
            if (obj->internal_kind == PS_INTERNAL_NUMMAP) {
                size_t index = 0;
                if (ps_string_to_index_size(key_val.as.string, &index)) {
                    PSValue mapped = ps_value_undefined();
                    if (ps_num_map_get(obj, index, &mapped)) {
                        if (mapped.type != PS_T_NUMBER) return 0;
                        *out = mapped.as.number;
                        return 1;
                    }
                }
            }
            int found = 0;
            PSValue val = ps_object_get(obj, key_val.as.string, &found);
            if (!found || val.type != PS_T_NUMBER) return 0;
            *out = val.as.number;
            return 1;
        }
        if (key_val.type == PS_T_NUMBER) {
            double num = key_val.as.number;
            if (num >= 0.0 && num <= (double)SIZE_MAX) {
                size_t index = (size_t)num;
                if ((double)index == num && obj->internal_kind == PS_INTERNAL_NUMMAP) {
                    PSValue mapped = ps_value_undefined();
                    if (ps_num_map_get(obj, index, &mapped)) {
                        if (mapped.type != PS_T_NUMBER) return 0;
                        *out = mapped.as.number;
                        return 1;
                    }
                }
            }
            PSString *name = ps_value_to_string(&key_val);
            int found = 0;
            PSValue val = ps_object_get(obj, name, &found);
            if (!found || val.type != PS_T_NUMBER) return 0;
            *out = val.as.number;
            return 1;
        }
    }
    return 0;
}

static int ps_fast_num_eval_call(PSFastNumCtx *ctx, PSAstNode *expr, double *out) {
    if (!ctx || !expr || expr->kind != AST_CALL || !out) return 0;
    uint8_t math_id = expr->as.call.fast_num_math_id;
    if (math_id == PS_FAST_NUM_MATH_NONE) {
        math_id = ps_fast_num_math_id(expr);
        expr->as.call.fast_num_math_id = math_id;
    }
    if (math_id == PS_FAST_NUM_MATH_NONE) return 0;

    size_t argc = expr->as.call.argc;
    double arg0 = 0.0;
    double arg1 = 0.0;
    if (argc > 0) {
        if (!ps_fast_num_eval_expr(ctx, expr->as.call.args[0], &arg0)) return 0;
    }
    if (argc > 1) {
        if (!ps_fast_num_eval_expr(ctx, expr->as.call.args[1], &arg1)) return 0;
    }

    if (math_id == PS_FAST_NUM_MATH_SQRT) {
        *out = sqrt(arg0);
        return 1;
    }
    if (math_id == PS_FAST_NUM_MATH_ABS) {
        *out = fabs(arg0);
        return 1;
    }
    if (math_id == PS_FAST_NUM_MATH_POW) {
        *out = pow(arg0, arg1);
        return 1;
    }
    if (math_id == PS_FAST_NUM_MATH_FLOOR) {
        *out = floor(arg0);
        return 1;
    }
    if (math_id == PS_FAST_NUM_MATH_CEIL) {
        *out = ceil(arg0);
        return 1;
    }
    if (math_id == PS_FAST_NUM_MATH_ROUND) {
        if (ps_value_is_nan(arg0) || isinf(arg0)) {
            *out = arg0;
            return 1;
        }
        double r = floor(arg0 + 0.5);
        if (r == 0.0 && arg0 < 0.0) r = -0.0;
        *out = r;
        return 1;
    }
    if (math_id == PS_FAST_NUM_MATH_MIN) {
        if (argc == 0) {
            *out = INFINITY;
            return 1;
        }
        double minv = INFINITY;
        for (size_t i = 0; i < argc; i++) {
            double v = 0.0;
            if (!ps_fast_num_eval_expr(ctx, expr->as.call.args[i], &v)) return 0;
            if (ps_value_is_nan(v)) {
                *out = 0.0 / 0.0;
                return 1;
            }
            if (v < minv) minv = v;
        }
        *out = minv;
        return 1;
    }
    if (math_id == PS_FAST_NUM_MATH_MAX) {
        if (argc == 0) {
            *out = -INFINITY;
            return 1;
        }
        double maxv = -INFINITY;
        for (size_t i = 0; i < argc; i++) {
            double v = 0.0;
            if (!ps_fast_num_eval_expr(ctx, expr->as.call.args[i], &v)) return 0;
            if (ps_value_is_nan(v)) {
                *out = 0.0 / 0.0;
                return 1;
            }
            if (v > maxv) maxv = v;
        }
        *out = maxv;
        return 1;
    }
    return 0;
}

static int ps_fast_num_eval_cond(PSFastNumCtx *ctx, PSAstNode *expr, int *out) {
    if (!ctx || !expr || !out) return 0;
    if (expr->kind == AST_LITERAL) {
        if (expr->as.literal.value.type == PS_T_BOOLEAN) {
            *out = expr->as.literal.value.as.boolean ? 1 : 0;
            return 1;
        }
        if (expr->as.literal.value.type == PS_T_NUMBER) {
            double v = expr->as.literal.value.as.number;
            *out = (!ps_value_is_nan(v) && v != 0.0) ? 1 : 0;
            return 1;
        }
        return 0;
    }
    if (expr->kind == AST_IDENTIFIER) {
        PSValue v = ps_value_undefined();
        if (!ps_fast_num_identifier_value(ctx, expr, &v)) return 0;
        if (v.type == PS_T_BOOLEAN) {
            *out = v.as.boolean ? 1 : 0;
            return 1;
        }
        if (v.type == PS_T_NUMBER) {
            *out = (!ps_value_is_nan(v.as.number) && v.as.number != 0.0) ? 1 : 0;
            return 1;
        }
        return 0;
    }
    if (expr->kind == AST_UNARY && expr->as.unary.op == TOK_NOT) {
        int inner = 0;
        if (!ps_fast_num_eval_cond(ctx, expr->as.unary.expr, &inner)) return 0;
        *out = inner ? 0 : 1;
        return 1;
    }
    if (expr->kind == AST_BINARY) {
        int op = expr->as.binary.op;
        if (op == TOK_AND_AND) {
            int left = 0;
            if (!ps_fast_num_eval_cond(ctx, expr->as.binary.left, &left)) return 0;
            if (!left) {
                *out = 0;
                return 1;
            }
            return ps_fast_num_eval_cond(ctx, expr->as.binary.right, out);
        }
        if (op == TOK_OR_OR) {
            int left = 0;
            if (!ps_fast_num_eval_cond(ctx, expr->as.binary.left, &left)) return 0;
            if (left) {
                *out = 1;
                return 1;
            }
            return ps_fast_num_eval_cond(ctx, expr->as.binary.right, out);
        }
        if (op == TOK_LT || op == TOK_LTE || op == TOK_GT || op == TOK_GTE ||
            op == TOK_EQ || op == TOK_NEQ || op == TOK_STRICT_EQ || op == TOK_STRICT_NEQ) {
            double l = 0.0;
            double r = 0.0;
            if (!ps_fast_num_eval_expr(ctx, expr->as.binary.left, &l)) return 0;
            if (!ps_fast_num_eval_expr(ctx, expr->as.binary.right, &r)) return 0;
            int ln = ps_value_is_nan(l);
            int rn = ps_value_is_nan(r);
            if (op == TOK_EQ || op == TOK_STRICT_EQ) {
                if (ln || rn) {
                    *out = 0;
                } else {
                    *out = (l == r) ? 1 : 0;
                }
                return 1;
            }
            if (op == TOK_NEQ || op == TOK_STRICT_NEQ) {
                if (ln || rn) {
                    *out = 1;
                } else {
                    *out = (l != r) ? 1 : 0;
                }
                return 1;
            }
            if (ln || rn) {
                *out = 0;
                return 1;
            }
            if (op == TOK_LT) *out = (l < r) ? 1 : 0;
            else if (op == TOK_LTE) *out = (l <= r) ? 1 : 0;
            else if (op == TOK_GT) *out = (l > r) ? 1 : 0;
            else *out = (l >= r) ? 1 : 0;
            return 1;
        }
    }
    {
        double v = 0.0;
        if (!ps_fast_num_eval_expr(ctx, expr, &v)) return 0;
        *out = (!ps_value_is_nan(v) && v != 0.0) ? 1 : 0;
        return 1;
    }
}

static int ps_fast_num_eval_expr(PSFastNumCtx *ctx, PSAstNode *expr, double *out) {
    if (!ctx || !expr || !out) return 0;
    switch (expr->kind) {
        case AST_LITERAL:
            if (expr->as.literal.value.type != PS_T_NUMBER) return 0;
            *out = expr->as.literal.value.as.number;
            return 1;
        case AST_IDENTIFIER: {
            uint8_t kind = expr->as.identifier.fast_num_kind;
            if (kind == PS_FAST_NUM_KIND_PARAM) {
                size_t idx = (size_t)expr->as.identifier.fast_num_index;
                if (idx >= (size_t)ctx->argc) return 0;
                if (!ctx->argv || ctx->argv[idx].type != PS_T_NUMBER) return 0;
                *out = ctx->argv[idx].as.number;
                return 1;
            }
            if (kind == PS_FAST_NUM_KIND_LOCAL) {
                size_t idx = (size_t)expr->as.identifier.fast_num_index;
                if (idx >= ctx->local_count) return 0;
                *out = ctx->local_vals[idx];
                return 1;
            }
            if (kind == PS_FAST_NUM_KIND_ENV) {
                if (ctx->func && ctx->func->env) {
                    int found = 0;
                    PSValue v = ps_value_undefined();
                    if (!ps_identifier_cached_get(ctx->func->env, expr, &v, &found)) {
                        PSString *name = ps_identifier_string(expr);
                        if (!name) return 0;
                        v = ps_env_get(ctx->func->env, name, &found);
                    }
                    if (found && v.type == PS_T_NUMBER) {
                        *out = v.as.number;
                        return 1;
                    }
                }
                return 0;
            }
            PSString *name = ps_identifier_string(expr);
            return ps_fast_num_lookup(ctx, name, out);
        }
        case AST_UNARY: {
            double v = 0.0;
            if (!ps_fast_num_eval_expr(ctx, expr->as.unary.expr, &v)) return 0;
            if (expr->as.unary.op == TOK_MINUS) v = -v;
            *out = v;
            return 1;
        }
        case AST_CONDITIONAL: {
            int cond = 0;
            if (!ps_fast_num_eval_cond(ctx, expr->as.conditional.cond, &cond)) return 0;
            if (cond) {
                return ps_fast_num_eval_expr(ctx, expr->as.conditional.then_expr, out);
            }
            return ps_fast_num_eval_expr(ctx, expr->as.conditional.else_expr, out);
        }
        case AST_BINARY: {
            double l = 0.0;
            double r = 0.0;
            if (!ps_fast_num_eval_expr(ctx, expr->as.binary.left, &l)) return 0;
            if (!ps_fast_num_eval_expr(ctx, expr->as.binary.right, &r)) return 0;
            switch (expr->as.binary.op) {
                case TOK_PLUS: *out = l + r; return 1;
                case TOK_MINUS: *out = l - r; return 1;
                case TOK_STAR: *out = l * r; return 1;
                case TOK_SLASH: *out = l / r; return 1;
                case TOK_PERCENT: *out = fmod(l, r); return 1;
                default: return 0;
            }
        }
        case AST_MEMBER:
            return ps_fast_num_eval_member(ctx, expr, out);
        case AST_CALL:
            return ps_fast_num_eval_call(ctx, expr, out);
        default:
            return 0;
    }
}

static void ps_fast_num_prepare(PSFunction *func) {
    if (!func || func->is_native) return;
    if (func->fast_checked & PS_FAST_CHECKED_NUM) return;
    func->fast_checked |= PS_FAST_CHECKED_NUM;
    if (!func->body || func->body->kind != AST_BLOCK) return;

    PSAstNode **items = func->body->as.list.items;
    size_t count = func->body->as.list.count;
    if (!items || count == 0) return;

    size_t local_count = 0;
    PSAstNode *ret_expr = NULL;
    PSAstNode *if_cond = NULL;
    PSAstNode *if_ret_expr = NULL;
    int saw_if = 0;

    for (size_t i = 0; i < count; i++) {
        PSAstNode *stmt = items[i];
        if (!stmt) return;
        if (stmt->kind == AST_VAR_DECL) {
            PSAstNode *id = stmt->as.var_decl.id;
            PSAstNode *init = stmt->as.var_decl.init;
            if (!id || id->kind != AST_IDENTIFIER || !init) return;
            if (!ps_fast_num_expr_ok(func, init)) return;
            local_count++;
            continue;
        }
        if (stmt->kind == AST_IF) {
            if (saw_if) return;
            PSAstNode *cond = stmt->as.if_stmt.cond;
            PSAstNode *then_branch = stmt->as.if_stmt.then_branch;
            PSAstNode *else_branch = stmt->as.if_stmt.else_branch;
            if (!cond || !then_branch || else_branch) return;
            if (then_branch->kind != AST_RETURN || !then_branch->as.ret.expr) return;
            if (!ps_fast_num_cond_ok(func, cond)) return;
            if (!ps_fast_num_expr_ok(func, then_branch->as.ret.expr)) return;
            if_cond = cond;
            if_ret_expr = then_branch->as.ret.expr;
            saw_if = 1;
            continue;
        }
        if (stmt->kind == AST_RETURN) {
            if (!stmt->as.ret.expr) return;
            if (!ps_fast_num_expr_ok(func, stmt->as.ret.expr)) return;
            ret_expr = stmt->as.ret.expr;
            if (i + 1 != count) return;
            break;
        }
        return;
    }

    if (!ret_expr) return;
    if (local_count == 0) return;

    PSAstNode **inits = (PSAstNode **)calloc(local_count, sizeof(PSAstNode *));
    PSString **names = (PSString **)calloc(local_count, sizeof(PSString *));
    if (!inits || !names) {
        free(inits);
        free(names);
        return;
    }

    size_t idx = 0;
    for (size_t i = 0; i < count; i++) {
        PSAstNode *stmt = items[i];
        if (stmt && stmt->kind == AST_VAR_DECL) {
            PSAstNode *id = stmt->as.var_decl.id;
            PSAstNode *init = stmt->as.var_decl.init;
            if (!id || id->kind != AST_IDENTIFIER || !init) {
                free(inits);
                free(names);
                return;
            }
            PSString *name = ps_identifier_string(id);
            for (size_t j = 0; j < idx; j++) {
                if (names[j] && ps_string_equals(names[j], name)) {
                    free(inits);
                    free(names);
                    return;
                }
            }
            inits[idx] = init;
            names[idx] = name;
            idx++;
        }
    }
    if (idx != local_count) {
        free(inits);
        free(names);
        return;
    }

    func->fast_num_inits = inits;
    func->fast_num_names = names;
    func->fast_num_count = local_count;
    func->fast_num_return = ret_expr;
    func->fast_num_if_cond = if_cond;
    func->fast_num_if_return = if_ret_expr;
    func->fast_flags |= PS_FAST_FLAG_NUM;
    for (size_t i = 0; i < local_count; i++) {
        ps_fast_num_bind_expr(func, inits[i]);
    }
    if (if_cond) {
        ps_fast_num_bind_expr(func, if_cond);
    }
    if (if_ret_expr) {
        ps_fast_num_bind_expr(func, if_ret_expr);
    }
    ps_fast_num_bind_expr(func, ret_expr);
    if (!func->fast_num_if_cond) {
        (void)ps_fast_num_compile_ops(func, inits, local_count, ret_expr);
    }
}

static int ps_fast_num_eval_ops(PSFastNumCtx *ctx,
                                PSFastNumOp *ops,
                                size_t op_count,
                                double *out) {
    if (!ctx || !ops || !out || op_count == 0) return 0;
    double stack_buf[64];
    double *stack = stack_buf;
    size_t sp = 0;
    if (op_count > 64) {
        stack = (double *)calloc(op_count, sizeof(double));
        if (!stack) return 0;
    }
    for (size_t ip = 0; ip < op_count; ip++) {
        PSFastNumOp *op = &ops[ip];
        switch (op->op) {
            case PS_FAST_NUM_OP_CONST:
                stack[sp++] = op->imm;
                break;
            case PS_FAST_NUM_OP_PARAM: {
                size_t idx = (size_t)op->index;
                if (idx >= (size_t)ctx->argc) goto fail;
                if (!ctx->argv || ctx->argv[idx].type != PS_T_NUMBER) goto fail;
                stack[sp++] = ctx->argv[idx].as.number;
                break;
            }
            case PS_FAST_NUM_OP_LOCAL: {
                size_t idx = (size_t)op->index;
                if (idx >= ctx->local_count) goto fail;
                stack[sp++] = ctx->local_vals[idx];
                break;
            }
            case PS_FAST_NUM_OP_ENV: {
                PSAstNode *node = op->node;
                if (!ctx->func || !ctx->func->env || !node) goto fail;
                int found = 0;
                PSValue v = ps_value_undefined();
                if (!ps_identifier_cached_get(ctx->func->env, node, &v, &found)) {
                    PSString *name = ps_identifier_string(node);
                    if (!name) goto fail;
                    v = ps_env_get(ctx->func->env, name, &found);
                }
                if (!found || v.type != PS_T_NUMBER) goto fail;
                stack[sp++] = v.as.number;
                break;
            }
            case PS_FAST_NUM_OP_STORE_LOCAL: {
                size_t idx = (size_t)op->index;
                if (idx >= ctx->local_count || sp == 0) goto fail;
                ctx->local_vals[idx] = stack[--sp];
                break;
            }
            case PS_FAST_NUM_OP_MEMBER: {
                PSAstNode *node = op->node;
                PSValue obj_val = ps_value_undefined();
                if (!ps_fast_num_identifier_value(ctx, node, &obj_val)) goto fail;
                if (obj_val.type != PS_T_OBJECT || !obj_val.as.object) goto fail;
                PSObject *obj = obj_val.as.object;
                size_t index = (size_t)op->index;
                if (obj->kind == PS_OBJ_KIND_ARRAY) {
                    PSValue elem = ps_value_undefined();
                    if (!ps_array_get_index(obj, index, &elem)) goto fail;
                    if (elem.type != PS_T_NUMBER) goto fail;
                    stack[sp++] = elem.as.number;
                    break;
                }
                if (obj->kind == PS_OBJ_KIND_BUFFER) {
                    PSBuffer *buf = ps_buffer_from_object(obj);
                    if (!buf || index >= buf->size) goto fail;
                    stack[sp++] = (double)buf->data[index];
                    break;
                }
                goto fail;
            }
            case PS_FAST_NUM_OP_NEG:
                if (sp == 0) goto fail;
                stack[sp - 1] = -stack[sp - 1];
                break;
            case PS_FAST_NUM_OP_ADD:
                if (sp < 2) goto fail;
                stack[sp - 2] = stack[sp - 2] + stack[sp - 1];
                sp--;
                break;
            case PS_FAST_NUM_OP_SUB:
                if (sp < 2) goto fail;
                stack[sp - 2] = stack[sp - 2] - stack[sp - 1];
                sp--;
                break;
            case PS_FAST_NUM_OP_MUL:
                if (sp < 2) goto fail;
                stack[sp - 2] = stack[sp - 2] * stack[sp - 1];
                sp--;
                break;
            case PS_FAST_NUM_OP_DIV:
                if (sp < 2) goto fail;
                stack[sp - 2] = stack[sp - 2] / stack[sp - 1];
                sp--;
                break;
            case PS_FAST_NUM_OP_MOD:
                if (sp < 2) goto fail;
                stack[sp - 2] = fmod(stack[sp - 2], stack[sp - 1]);
                sp--;
                break;
            case PS_FAST_NUM_OP_MATH_SQRT:
                if (sp == 0) goto fail;
                stack[sp - 1] = sqrt(stack[sp - 1]);
                break;
            case PS_FAST_NUM_OP_MATH_ABS:
                if (sp == 0) goto fail;
                stack[sp - 1] = fabs(stack[sp - 1]);
                break;
            case PS_FAST_NUM_OP_MATH_POW:
                if (sp < 2) goto fail;
                stack[sp - 2] = pow(stack[sp - 2], stack[sp - 1]);
                sp--;
                break;
            case PS_FAST_NUM_OP_MATH_FLOOR:
                if (sp == 0) goto fail;
                stack[sp - 1] = floor(stack[sp - 1]);
                break;
            case PS_FAST_NUM_OP_MATH_CEIL:
                if (sp == 0) goto fail;
                stack[sp - 1] = ceil(stack[sp - 1]);
                break;
            case PS_FAST_NUM_OP_MATH_ROUND:
                if (sp == 0) goto fail;
                if (!ps_value_is_nan(stack[sp - 1]) && !isinf(stack[sp - 1])) {
                    double r = floor(stack[sp - 1] + 0.5);
                    if (r == 0.0 && stack[sp - 1] < 0.0) r = -0.0;
                    stack[sp - 1] = r;
                }
                break;
            case PS_FAST_NUM_OP_MATH_MIN: {
                uint32_t argc = op->index;
                if (argc == 0) {
                    stack[sp++] = INFINITY;
                    break;
                }
                if (sp < argc) goto fail;
                double minv = INFINITY;
                for (uint32_t i = 0; i < argc; i++) {
                    double v = stack[sp - 1 - i];
                    if (ps_value_is_nan(v)) {
                        stack[sp - argc] = 0.0 / 0.0;
                        sp = sp - argc + 1;
                        goto next_op;
                    }
                    if (v < minv) minv = v;
                }
                stack[sp - argc] = minv;
                sp = sp - argc + 1;
                break;
            }
            case PS_FAST_NUM_OP_MATH_MAX: {
                uint32_t argc = op->index;
                if (argc == 0) {
                    stack[sp++] = -INFINITY;
                    break;
                }
                if (sp < argc) goto fail;
                double maxv = -INFINITY;
                for (uint32_t i = 0; i < argc; i++) {
                    double v = stack[sp - 1 - i];
                    if (ps_value_is_nan(v)) {
                        stack[sp - argc] = 0.0 / 0.0;
                        sp = sp - argc + 1;
                        goto next_op;
                    }
                    if (v > maxv) maxv = v;
                }
                stack[sp - argc] = maxv;
                sp = sp - argc + 1;
                break;
            }
            case PS_FAST_NUM_OP_RETURN:
                if (sp == 0) goto fail;
                *out = stack[--sp];
                if (stack != stack_buf) free(stack);
                return 1;
            default:
                goto fail;
        }
next_op:
        ;
    }
fail:
    if (stack != stack_buf) free(stack);
    return 0;
}

static int ps_fast_num_eval(PSVM *vm,
                            PSFunction *func,
                            int argc,
                            PSValue *argv,
                            double *out) {
    if (!vm || !func || !out) return 0;
    if (!(func->fast_flags & PS_FAST_FLAG_NUM)) return 0;
    if (!func->fast_num_inits || !func->fast_num_names || !func->fast_num_return) return 0;

    double stack_vals[8];
    double *locals = stack_vals;
    if (func->fast_num_count > 8) {
        locals = (double *)calloc(func->fast_num_count, sizeof(double));
        if (!locals) return 0;
    }

    PSFastNumCtx ctx = {
        .vm = vm,
        .func = func,
        .argc = argc,
        .argv = argv,
        .local_names = func->fast_num_names,
        .local_vals = locals,
        .local_count = func->fast_num_count
    };

    if (!func->fast_num_if_cond && func->fast_num_ops && func->fast_num_ops_count > 0) {
        if (ps_fast_num_eval_ops(&ctx, func->fast_num_ops, func->fast_num_ops_count, out)) {
            if (locals != stack_vals) free(locals);
            return 1;
        }
    }

    for (size_t i = 0; i < func->fast_num_count; i++) {
        double v = 0.0;
        if (!ps_fast_num_eval_expr(&ctx, func->fast_num_inits[i], &v)) {
            if (locals != stack_vals) free(locals);
            return 0;
        }
        locals[i] = v;
    }

    if (func->fast_num_if_cond && func->fast_num_if_return) {
        int cond = 0;
        if (!ps_fast_num_eval_cond(&ctx, func->fast_num_if_cond, &cond)) {
            if (locals != stack_vals) free(locals);
            return 0;
        }
        if (cond) {
            double result = 0.0;
            if (!ps_fast_num_eval_expr(&ctx, func->fast_num_if_return, &result)) {
                if (locals != stack_vals) free(locals);
                return 0;
            }
            if (locals != stack_vals) free(locals);
            *out = result;
            return 1;
        }
    }

    double result = 0.0;
    int ok = ps_fast_num_eval_expr(&ctx, func->fast_num_return, &result);
    if (locals != stack_vals) free(locals);
    if (!ok) return 0;
    *out = result;
    return 1;
}

static void ps_fast_clamp_prepare(PSFunction *func) {
    if (!func || func->is_native) return;
    if (func->fast_checked & PS_FAST_CHECKED_CLAMP) return;
    func->fast_checked |= PS_FAST_CHECKED_CLAMP;
    if (func->param_count != 1 || !func->params || !func->params[0]) return;
    if (func->param_defaults && func->param_defaults[0]) return;
    if (!func->body || func->body->kind != AST_BLOCK) return;
    if (func->body->as.list.count != 3) return;

    PSAstNode *param = func->params[0];
    if (!param || param->kind != AST_IDENTIFIER) return;
    PSString *param_name = ps_identifier_string(param);
    if (!param_name) return;

    PSAstNode *stmt0 = func->body->as.list.items[0];
    PSAstNode *stmt1 = func->body->as.list.items[1];
    PSAstNode *stmt2 = func->body->as.list.items[2];
    if (!stmt0 || !stmt1 || !stmt2) return;
    if (stmt0->kind != AST_IF || stmt1->kind != AST_IF || stmt2->kind != AST_RETURN) return;

    PSAstNode *cond0 = stmt0->as.if_stmt.cond;
    PSAstNode *then0 = stmt0->as.if_stmt.then_branch;
    if (!cond0 || !then0 || stmt0->as.if_stmt.else_branch) return;
    if (cond0->kind != AST_BINARY || cond0->as.binary.op != TOK_LT) return;
    if (!ps_match_identifier_node(cond0->as.binary.left, param_name)) return;
    if (!cond0->as.binary.right ||
        cond0->as.binary.right->kind != AST_LITERAL ||
        cond0->as.binary.right->as.literal.value.type != PS_T_NUMBER) {
        return;
    }
    double min_val = cond0->as.binary.right->as.literal.value.as.number;
    if (then0->kind != AST_RETURN || !then0->as.ret.expr) return;
    if (then0->as.ret.expr->kind != AST_LITERAL ||
        then0->as.ret.expr->as.literal.value.type != PS_T_NUMBER) return;
    if (then0->as.ret.expr->as.literal.value.as.number != min_val) return;

    PSAstNode *cond1 = stmt1->as.if_stmt.cond;
    PSAstNode *then1 = stmt1->as.if_stmt.then_branch;
    if (!cond1 || !then1 || stmt1->as.if_stmt.else_branch) return;
    if (cond1->kind != AST_BINARY || cond1->as.binary.op != TOK_GT) return;
    if (!ps_match_identifier_node(cond1->as.binary.left, param_name)) return;
    if (cond1->as.binary.right->kind != AST_LITERAL ||
        cond1->as.binary.right->as.literal.value.type != PS_T_NUMBER) return;
    double max_val = cond1->as.binary.right->as.literal.value.as.number;
    if (then1->kind != AST_RETURN || !then1->as.ret.expr) return;
    if (then1->as.ret.expr->kind != AST_LITERAL ||
        then1->as.ret.expr->as.literal.value.type != PS_T_NUMBER) return;
    double ret_max = then1->as.ret.expr->as.literal.value.as.number;
    if (ret_max != max_val) return;

    PSAstNode *ret_expr = stmt2->as.ret.expr;
    int use_floor = 0;
    if (ps_match_identifier_node(ret_expr, param_name)) {
        use_floor = 0;
    } else if (ps_match_math_floor_call(ret_expr, param_name)) {
        use_floor = 1;
    } else {
        return;
    }

    func->fast_clamp_min = min_val;
    func->fast_clamp_max = max_val;
    func->fast_clamp_use_floor = (uint8_t)use_floor;
    func->fast_flags |= PS_FAST_FLAG_CLAMP;
}

static int ps_string_is_length(const PSString *s) {
    static const char *length_str = "length";
    if (!s || s->byte_len != 6) return 0;
    return memcmp(s->utf8, length_str, 6) == 0;
}

static int ps_string_to_k_index(const PSString *name, uint32_t *out_index) {
    if (!name || name->byte_len < 2 || !name->utf8) return 0;
    if (name->utf8[0] != 'k') return 0;
    const unsigned char *p = (const unsigned char *)name->utf8 + 1;
    size_t len = name->byte_len - 1;
    if (len > 1 && p[0] == '0') return 0;
    unsigned long long value = 0;
    for (size_t i = 0; i < len; i++) {
        if (p[i] < '0' || p[i] > '9') return 0;
        value = value * 10ULL + (unsigned long long)(p[i] - '0');
        if (value > UINT32_MAX) return 0;
    }
    if (out_index) *out_index = (uint32_t)value;
    return 1;
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
        char msg[96];
        snprintf(msg, sizeof(msg), "Invalid array length: %.15g", num);
        ctl->throw_value = ps_vm_make_error(vm, "RangeError", msg);
        return -1;
    }
    size_t new_len = (size_t)num;
    if (!ps_array_set_length_internal(obj, new_len)) return 0;
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
            PSString *name = ps_identifier_string(target);
            ps_env_define(env, name, value);
        }
        return 1;
    }
    if (target->kind == AST_IDENTIFIER) {
        PSString *name = ps_identifier_string(target);
        ps_env_set(env, name, value);
        return 1;
    }
    if (target->kind == AST_MEMBER) {
        PSValue target_val = eval_expression(vm, env, target->as.member.object, ctl);
        if (ctl->did_throw) return 0;
        PSObject *target_obj = ps_to_object(vm, &target_val, ctl);
        if (ctl->did_throw) return 0;
        if (target_obj && target_obj->kind == PS_OBJ_KIND_BUFFER && target->as.member.computed) {
            PSValue key_val = eval_expression(vm, env, target->as.member.property, ctl);
            if (ctl->did_throw) return 0;
            int handled = ps_buffer_write_index_value(vm, target_obj, key_val, value, ctl);
            if (handled < 0) return 0;
            if (handled == 0) {
                PSString *prop = ps_to_string(vm, key_val);
                if (ps_check_pending_throw(vm, ctl)) return 0;
                handled = ps_buffer_write_index(vm, target_obj, prop, value, ctl);
                if (handled < 0) return 0;
                if (handled == 0) {
                    ps_object_put(target_obj, prop, value);
                    (void)ps_env_update_arguments(env, target_obj, prop, value);
                }
            }
        } else if (target_obj && target_obj->kind == PS_OBJ_KIND_BUFFER32 && target->as.member.computed) {
            PSValue key_val = eval_expression(vm, env, target->as.member.property, ctl);
            if (ctl->did_throw) return 0;
            int handled = ps_buffer32_write_index_value(vm, target_obj, key_val, value, ctl);
            if (handled < 0) return 0;
            if (handled == 0) {
                PSString *prop = ps_to_string(vm, key_val);
                if (ps_check_pending_throw(vm, ctl)) return 0;
                handled = ps_buffer32_write_index(vm, target_obj, prop, value, ctl);
                if (handled < 0) return 0;
                if (handled == 0) {
                    ps_object_put(target_obj, prop, value);
                    (void)ps_env_update_arguments(env, target_obj, prop, value);
                }
            }
        } else if (target_obj && target_obj->kind == PS_OBJ_KIND_ARRAY && target->as.member.computed) {
            PSValue key_val = eval_expression(vm, env, target->as.member.property, ctl);
            if (ctl->did_throw) return 0;
            int handled = ps_array_write_index_value(vm, target_obj, key_val, value, ctl);
            if (handled < 0) return 0;
            if (handled == 0) {
                PSString *prop = ps_to_string(vm, key_val);
                if (ps_check_pending_throw(vm, ctl)) return 0;
                ps_object_put(target_obj, prop, value);
                ps_array_update_length(target_obj, prop);
                (void)ps_env_update_arguments(env, target_obj, prop, value);
            }
        } else if (target_obj && target_obj->kind == PS_OBJ_KIND_PLAIN && target->as.member.computed) {
            PSValue key_val = eval_expression(vm, env, target->as.member.property, ctl);
            if (ctl->did_throw) return 0;
            size_t index = 0;
            int idx = ps_value_to_index(vm, key_val, &index, ctl);
            if (idx < 0) return 0;
            if (idx > 0 && (target_obj->internal == NULL || target_obj->internal_kind == PS_INTERNAL_NUMMAP)) {
                int is_new = 0;
                if (!ps_num_map_set(target_obj, index, value, &is_new)) return 0;
                if (is_new) target_obj->shape_id++;
                if (env && env->arguments_obj == target_obj) {
                    PSString *prop = ps_array_index_string(vm, index);
                    (void)ps_env_update_arguments(env, target_obj, prop, value);
                }
            } else {
                PSString *prop = ps_to_string(vm, key_val);
                if (ps_check_pending_throw(vm, ctl)) return 0;
                ps_object_put(target_obj, prop, value);
                (void)ps_env_update_arguments(env, target_obj, prop, value);
            }
        } else {
            PSString *prop = ps_member_key(vm, env, target, ctl);
            if (ctl->did_throw) return 0;
            int handled = ps_buffer_write_index(vm, target_obj, prop, value, ctl);
            if (handled < 0) return 0;
            if (handled == 0) {
                ps_object_put(target_obj, prop, value);
                (void)ps_env_update_arguments(env, target_obj, prop, value);
            }
        }
        return 1;
    }
    return 0;
}

static uint8_t ps_clamp_byte(double num) {
    if (isnan(num) || isinf(num)) return 0;
    if (num <= 0.0) return 0;
    if (num >= 255.0) return 255;
    if (num < 1.0) return 0;
    return (uint8_t)floor(num + 0.5);
}

static uint32_t ps_clamp_u32(double num) {
    if (isnan(num) || isinf(num)) return 0;
    if (num <= 0.0) return 0;
    if (num >= 4294967295.0) return 4294967295u;
    if (num < 1.0) return 0;
    return (uint32_t)floor(num + 0.5);
}

static int ps_buffer_index_from_prop(PSObject *obj, PSString *prop, size_t *out_index) {
    if (!obj || obj->kind != PS_OBJ_KIND_BUFFER || !prop) return 0;
    return ps_array_string_to_index(prop, out_index);
}

static int ps_buffer32_index_from_prop(PSObject *obj, PSString *prop, size_t *out_index) {
    if (!obj || obj->kind != PS_OBJ_KIND_BUFFER32 || !prop) return 0;
    return ps_array_string_to_index(prop, out_index);
}

static int ps_string_to_index_size(PSString *name, size_t *out_index) {
    if (!name || !name->utf8) return 0;
    if (name->index_state == 1) {
        if (out_index) *out_index = name->index_value;
        return 1;
    }
    if (name->index_state == 2) return 0;
    if (name->byte_len == 0) {
        name->index_state = 1;
        name->index_value = 0;
        if (out_index) *out_index = 0;
        return 1;
    }
    size_t value = 0;
    for (size_t i = 0; i < name->byte_len; i++) {
        unsigned char c = (unsigned char)name->utf8[i];
        if (c < '0' || c > '9') {
            name->index_state = 2;
            return 0;
        }
        size_t digit = (size_t)(c - '0');
        if (value > (SIZE_MAX - digit) / 10) {
            name->index_state = 2;
            return 0;
        }
        value = (value * 10) + digit;
    }
    name->index_state = 1;
    name->index_value = value;
    if (out_index) *out_index = value;
    return 1;
}

static int ps_value_to_index(PSVM *vm, PSValue value, size_t *out_index, PSEvalControl *ctl) {
    if (value.type == PS_T_NUMBER) {
        double num = value.as.number;
        if (num >= 0.0 && num <= (double)SIZE_MAX) {
            size_t idx = (size_t)num;
            if ((double)idx == num) {
                if (out_index) *out_index = idx;
                return 1;
            }
        }
        return 0;
    }
    if (value.type == PS_T_STRING) {
        if (ps_string_to_index_size(value.as.string, out_index)) return 1;
    }
    double num = ps_to_number(vm, value);
    if (ps_check_pending_throw(vm, ctl)) return -1;
    if (num >= 0.0 && num <= (double)SIZE_MAX) {
        size_t idx = (size_t)num;
        if ((double)idx == num) {
            if (out_index) *out_index = idx;
            return 1;
        }
    }
    return 0;
}

static int ps_value_to_array_index(PSVM *vm, PSValue value, size_t *out_index, PSEvalControl *ctl) {
    if (value.type == PS_T_NUMBER) {
        double num = value.as.number;
        if (num >= 0.0 && num < 4294967295.0) {
            size_t idx = (size_t)num;
            if ((double)idx == num) {
                if (out_index) *out_index = idx;
                return 1;
            }
        }
        return 0;
    }
    if (value.type == PS_T_STRING) {
        if (ps_array_string_to_index(value.as.string, out_index)) return 1;
        return 0;
    }
    PSString *key = ps_to_string(vm, value);
    if (ps_check_pending_throw(vm, ctl)) return -1;
    if (ps_array_string_to_index(key, out_index)) return 1;
    return 0;
}

static int ps_array_key_to_index_fast(PSValue key_val, size_t *out_index) {
    if (key_val.type == PS_T_NUMBER) {
        double num = key_val.as.number;
        if (num >= 0.0 && num < 4294967295.0) {
            size_t idx = (size_t)num;
            if ((double)idx == num) {
                if (out_index) *out_index = idx;
                return 1;
            }
        }
        return 0;
    }
    if (key_val.type == PS_T_STRING) {
        if (ps_array_string_to_index(key_val.as.string, out_index)) return 1;
    }
    return 0;
}

static int ps_array_read_index_fast_value(PSObject *obj,
                                          PSValue key_val,
                                          PSValue *out_value,
                                          size_t *out_index) {
    if (!obj || obj->kind != PS_OBJ_KIND_ARRAY) return 0;
    size_t index = 0;
    if (!ps_array_key_to_index_fast(key_val, &index)) return 0;
    if (out_index) *out_index = index;
    if (ps_array_get_index(obj, index, out_value)) return 1;
    return -1;
}

static int ps_array_write_index_fast_value(PSObject *obj,
                                           PSValue key_val,
                                           PSValue value,
                                           size_t *out_index) {
    if (!obj || obj->kind != PS_OBJ_KIND_ARRAY) return 0;
    size_t index = 0;
    if (!ps_array_key_to_index_fast(key_val, &index)) return 0;
    if (out_index) *out_index = index;
    return ps_array_set_index(obj, index, value) ? 1 : 0;
}

static int ps_array_read_index_value(PSVM *vm,
                                     PSObject *obj,
                                     PSValue key_val,
                                     PSValue *out_value,
                                     PSEvalControl *ctl) {
    if (!obj || obj->kind != PS_OBJ_KIND_ARRAY) return 0;
    size_t index = 0;
    int idx = ps_value_to_array_index(vm, key_val, &index, ctl);
    if (idx <= 0) return idx;
    if (ps_array_get_index(obj, index, out_value)) return 1;
    return 0;
}

static int ps_array_write_index_value(PSVM *vm,
                                      PSObject *obj,
                                      PSValue key_val,
                                      PSValue value,
                                      PSEvalControl *ctl) {
    if (!obj || obj->kind != PS_OBJ_KIND_ARRAY) return 0;
    size_t index = 0;
    int idx = ps_value_to_array_index(vm, key_val, &index, ctl);
    if (idx <= 0) return idx;
    (void)vm;
    (void)ctl;
    return ps_array_set_index(obj, index, value) ? 1 : 0;
}

static int ps_buffer_read_index_value(PSVM *vm,
                                      PSObject *obj,
                                      PSValue key_val,
                                      PSValue *out_value,
                                      PSEvalControl *ctl) {
    if (!obj || obj->kind != PS_OBJ_KIND_BUFFER) return 0;
    size_t index = 0;
    int idx = ps_value_to_index(vm, key_val, &index, ctl);
    if (idx <= 0) return idx;
    if (vm) vm->perf.buffer_read_index_fast++;
    PSBuffer *buf = ps_buffer_from_object(obj);
    if (!buf || index >= buf->size) {
        ctl->did_throw = 1;
        ctl->throw_value = ps_vm_make_error(vm, "RangeError", "Buffer index out of range");
        return -1;
    }
    if (out_value) {
        *out_value = ps_value_number((double)buf->data[index]);
    }
    return 1;
}

static int ps_member_cached_get(PSObject *obj,
                                PSAstNode *member,
                                PSString *prop,
                                PSValue *out) {
    if (!obj || !member || member->kind != AST_MEMBER) return 0;
    if (member->as.member.computed) {
        PSAstNode *lit = member->as.member.property;
        if (!lit || lit->kind != AST_LITERAL || lit->as.literal.value.type != PS_T_STRING) {
            return 0;
        }
    }
    if (member->as.member.cache_obj == obj &&
        member->as.member.cache_shape == obj->shape_id &&
        member->as.member.cache_prop &&
        member->as.member.cache_prop->name == prop) {
        if (out) *out = member->as.member.cache_prop->value;
        return 1;
    }
    PSProperty *p = ps_object_get_own_prop(obj, prop);
    if (!p) return 0;
    member->as.member.cache_obj = obj;
    member->as.member.cache_prop = p;
    member->as.member.cache_shape = obj->shape_id;
    if (out) *out = p->value;
    return 1;
}

static int ps_identifier_cached_get(PSEnv *env,
                                    PSAstNode *id,
                                    PSValue *out,
                                    int *found) {
    if (found) *found = 0;
    if (out) *out = ps_value_undefined();
    if (!env || !id || id->kind != AST_IDENTIFIER) return 0;
    PSString *name = ps_identifier_string(id);
    if (!name) return 0;
    if (ps_string_equals_cstr(name, "arguments")) {
        PSValue v = ps_env_get(env, name, found);
        if (found && *found && out) {
            *out = v;
        }
        return 1;
    }
    if (id->as.identifier.cache_fast_env == env) {
        size_t idx = id->as.identifier.cache_fast_index;
        if (env->fast_values && idx < env->fast_count) {
            if (out) *out = env->fast_values[idx];
            if (found) *found = 1;
            return 1;
        }
    }
    if (id->as.identifier.cache_env == env &&
        id->as.identifier.cache_record == env->record &&
        id->as.identifier.cache_prop &&
        env->record &&
        id->as.identifier.cache_shape == env->record->shape_id) {
        if (out) *out = id->as.identifier.cache_prop->value;
        if (found) *found = 1;
        return 1;
    }
    for (PSEnv *cur = env; cur; cur = cur->parent) {
        if (cur->fast_names && cur->fast_values) {
            for (size_t i = 0; i < cur->fast_count; i++) {
                if (cur->fast_names[i] && ps_string_equals(cur->fast_names[i], name)) {
                    id->as.identifier.cache_fast_env = cur;
                    id->as.identifier.cache_fast_index = i;
                    if (out) *out = cur->fast_values[i];
                    if (found) *found = 1;
                    return 1;
                }
            }
        }
        if (!cur->record) continue;
        PSProperty *prop = ps_object_get_own_prop(cur->record, name);
        if (!prop) continue;
        id->as.identifier.cache_env = cur;
        id->as.identifier.cache_record = cur->record;
        id->as.identifier.cache_prop = prop;
        id->as.identifier.cache_shape = cur->record->shape_id;
        if (out) *out = prop->value;
        if (found) *found = 1;
        return 1;
    }
    return 1;
}

#if PS_ENABLE_ARGUMENTS_ALIASING
static void ps_env_update_arguments_for_name(PSEnv *env, PSString *name, PSValue value) {
    if (!env || !env->arguments_obj || !env->param_names || !name) return;
    for (size_t i = 0; i < env->param_count; i++) {
        if (env->param_names[i] && ps_string_equals(env->param_names[i], name)) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%zu", i);
            ps_object_put(env->arguments_obj, ps_string_from_cstr(buf), value);
            break;
        }
    }
}
#endif

static int ps_identifier_cached_set(PSEnv *env,
                                    PSAstNode *id,
                                    PSValue value) {
    if (!env || !id || id->kind != AST_IDENTIFIER) return 0;
    PSString *name = ps_identifier_string(id);
    if (!name) return 0;
    if (ps_string_equals_cstr(name, "arguments")) return 0;
    if (id->as.identifier.cache_fast_env == env) {
        size_t idx = id->as.identifier.cache_fast_index;
        if (env->fast_values && idx < env->fast_count) {
            env->fast_values[idx] = value;
            if (env->record) {
                (void)ps_object_put(env->record, name, value);
            }
#if PS_ENABLE_ARGUMENTS_ALIASING
            ps_env_update_arguments_for_name(env, name, value);
#endif
            return 1;
        }
    }
    if (id->as.identifier.cache_env == env &&
        id->as.identifier.cache_record == env->record &&
        id->as.identifier.cache_prop &&
        env->record &&
        id->as.identifier.cache_shape == env->record->shape_id) {
        id->as.identifier.cache_prop->value = value;
#if PS_ENABLE_ARGUMENTS_ALIASING
        ps_env_update_arguments_for_name(env, name, value);
#endif
        return 1;
    }
    return 0;
}

static int ps_buffer_write_index_value(PSVM *vm,
                                      PSObject *obj,
                                      PSValue key_val,
                                      PSValue value,
                                      PSEvalControl *ctl) {
    if (!obj || obj->kind != PS_OBJ_KIND_BUFFER) return 0;
    size_t index = 0;
    int idx = ps_value_to_index(vm, key_val, &index, ctl);
    if (idx <= 0) return idx;
    if (vm) vm->perf.buffer_write_index_fast++;
    PSBuffer *buf = ps_buffer_from_object(obj);
    if (!buf || index >= buf->size) {
        ctl->did_throw = 1;
        ctl->throw_value = ps_vm_make_error(vm, "RangeError", "Buffer index out of range");
        return -1;
    }
    double num = ps_to_number(vm, value);
    if (ps_check_pending_throw(vm, ctl)) return -1;
    buf->data[index] = ps_clamp_byte(num);
    return 1;
}

static int ps_buffer_read_index(PSVM *vm,
                                PSObject *obj,
                                PSString *prop,
                                PSValue *out_value,
                                PSEvalControl *ctl) {
    size_t index = 0;
    if (!ps_buffer_index_from_prop(obj, prop, &index)) return 0;
    if (vm) vm->perf.buffer_read_index++;
    PSBuffer *buf = ps_buffer_from_object(obj);
    if (!buf || index >= buf->size) {
        ctl->did_throw = 1;
        ctl->throw_value = ps_vm_make_error(vm, "RangeError", "Buffer index out of range");
        return -1;
    }
    if (out_value) {
        *out_value = ps_value_number((double)buf->data[index]);
    }
    return 1;
}

static int ps_buffer32_read_index_value(PSVM *vm,
                                        PSObject *obj,
                                        PSValue key_val,
                                        PSValue *out_value,
                                        PSEvalControl *ctl) {
    if (!obj || obj->kind != PS_OBJ_KIND_BUFFER32) return 0;
    size_t index = 0;
    int idx = ps_value_to_index(vm, key_val, &index, ctl);
    if (idx <= 0) return idx;
    if (vm) vm->perf.buffer32_read_index_fast++;
    PSBuffer32 *view = ps_buffer32_from_object(obj);
    if (!view || !view->source) return 0;
    PSBuffer *buf = ps_buffer_from_object(view->source);
    if (!buf) return 0;
    if (index >= view->length) {
        ctl->did_throw = 1;
        ctl->throw_value = ps_vm_make_error(vm, "RangeError", "Buffer32 index out of range");
        return -1;
    }
    size_t base = view->offset + index * 4u;
    if (base + 3u >= buf->size) {
        ctl->did_throw = 1;
        ctl->throw_value = ps_vm_make_error(vm, "RangeError", "Buffer32 index out of range");
        return -1;
    }
    if (out_value) {
        uint32_t v = (uint32_t)buf->data[base] |
                     ((uint32_t)buf->data[base + 1] << 8) |
                     ((uint32_t)buf->data[base + 2] << 16) |
                     ((uint32_t)buf->data[base + 3] << 24);
        *out_value = ps_value_number((double)v);
    }
    return 1;
}

static int ps_buffer32_write_index_value(PSVM *vm,
                                         PSObject *obj,
                                         PSValue key_val,
                                         PSValue value,
                                         PSEvalControl *ctl) {
    if (!obj || obj->kind != PS_OBJ_KIND_BUFFER32) return 0;
    size_t index = 0;
    int idx = ps_value_to_index(vm, key_val, &index, ctl);
    if (idx <= 0) return idx;
    if (vm) vm->perf.buffer32_write_index_fast++;
    PSBuffer32 *view = ps_buffer32_from_object(obj);
    if (!view || !view->source) return 0;
    PSBuffer *buf = ps_buffer_from_object(view->source);
    if (!buf) return 0;
    if (index >= view->length) {
        ctl->did_throw = 1;
        ctl->throw_value = ps_vm_make_error(vm, "RangeError", "Buffer32 index out of range");
        return -1;
    }
    size_t base = view->offset + index * 4u;
    if (base + 3u >= buf->size) {
        ctl->did_throw = 1;
        ctl->throw_value = ps_vm_make_error(vm, "RangeError", "Buffer32 index out of range");
        return -1;
    }
    double num = ps_to_number(vm, value);
    if (ps_check_pending_throw(vm, ctl)) return -1;
    uint32_t v = ps_clamp_u32(num);
    buf->data[base] = (uint8_t)(v & 0xffu);
    buf->data[base + 1] = (uint8_t)((v >> 8) & 0xffu);
    buf->data[base + 2] = (uint8_t)((v >> 16) & 0xffu);
    buf->data[base + 3] = (uint8_t)((v >> 24) & 0xffu);
    return 1;
}

static int ps_buffer32_read_index(PSVM *vm,
                                  PSObject *obj,
                                  PSString *prop,
                                  PSValue *out_value,
                                  PSEvalControl *ctl) {
    size_t index = 0;
    if (!ps_buffer32_index_from_prop(obj, prop, &index)) return 0;
    if (vm) vm->perf.buffer32_read_index++;
    PSBuffer32 *view = ps_buffer32_from_object(obj);
    if (!view || !view->source) return 0;
    PSBuffer *buf = ps_buffer_from_object(view->source);
    if (!buf) return 0;
    if (index >= view->length) {
        ctl->did_throw = 1;
        ctl->throw_value = ps_vm_make_error(vm, "RangeError", "Buffer32 index out of range");
        return -1;
    }
    size_t base = view->offset + index * 4u;
    if (base + 3u >= buf->size) {
        ctl->did_throw = 1;
        ctl->throw_value = ps_vm_make_error(vm, "RangeError", "Buffer32 index out of range");
        return -1;
    }
    if (out_value) {
        uint32_t v = (uint32_t)buf->data[base] |
                     ((uint32_t)buf->data[base + 1] << 8) |
                     ((uint32_t)buf->data[base + 2] << 16) |
                     ((uint32_t)buf->data[base + 3] << 24);
        *out_value = ps_value_number((double)v);
    }
    return 1;
}

static int ps_buffer32_write_index(PSVM *vm,
                                   PSObject *obj,
                                   PSString *prop,
                                   PSValue value,
                                   PSEvalControl *ctl) {
    size_t index = 0;
    if (!ps_buffer32_index_from_prop(obj, prop, &index)) return 0;
    if (vm) vm->perf.buffer32_write_index++;
    PSBuffer32 *view = ps_buffer32_from_object(obj);
    if (!view || !view->source) return 0;
    PSBuffer *buf = ps_buffer_from_object(view->source);
    if (!buf) return 0;
    if (index >= view->length) {
        ctl->did_throw = 1;
        ctl->throw_value = ps_vm_make_error(vm, "RangeError", "Buffer32 index out of range");
        return -1;
    }
    size_t base = view->offset + index * 4u;
    if (base + 3u >= buf->size) {
        ctl->did_throw = 1;
        ctl->throw_value = ps_vm_make_error(vm, "RangeError", "Buffer32 index out of range");
        return -1;
    }
    double num = ps_to_number(vm, value);
    if (ps_check_pending_throw(vm, ctl)) return -1;
    uint32_t v = ps_clamp_u32(num);
    buf->data[base] = (uint8_t)(v & 0xffu);
    buf->data[base + 1] = (uint8_t)((v >> 8) & 0xffu);
    buf->data[base + 2] = (uint8_t)((v >> 16) & 0xffu);
    buf->data[base + 3] = (uint8_t)((v >> 24) & 0xffu);
    return 1;
}

static int ps_buffer_write_index(PSVM *vm,
                                 PSObject *obj,
                                 PSString *prop,
                                 PSValue value,
                                 PSEvalControl *ctl) {
    size_t index = 0;
    if (!ps_buffer_index_from_prop(obj, prop, &index)) return 0;
    if (vm) vm->perf.buffer_write_index++;
    PSBuffer *buf = ps_buffer_from_object(obj);
    if (!buf || index >= buf->size) {
        ctl->did_throw = 1;
        ctl->throw_value = ps_vm_make_error(vm, "RangeError", "Buffer index out of range");
        return -1;
    }
    double num = ps_to_number(vm, value);
    if (ps_check_pending_throw(vm, ctl)) return -1;
    buf->data[index] = ps_clamp_byte(num);
    return 1;
}

static void ps_array_update_length(PSObject *obj, PSString *prop) {
    if (!obj || obj->kind != PS_OBJ_KIND_ARRAY || !prop) return;
    size_t index = 0;
    if (!ps_array_string_to_index(prop, &index)) return;
    size_t new_len = index + 1;
    PSArray *arr = ps_array_from_object(obj);
    size_t len = arr ? arr->length : 0;
    if (new_len > len) {
        if (arr) {
            arr->length = new_len;
            return;
        }
        (void)ps_array_set_length_internal(obj, new_len);
    }
}

static int ps_eval_args(PSVM *vm,
                        PSEnv *env,
                        PSAstNode **args,
                        size_t argc,
                        PSValue *stack_buf,
                        size_t stack_cap,
                        PSValue **out_args,
                        int *out_heap,
                        PSEvalControl *ctl) {
    (void)vm;
    if (out_heap) *out_heap = 0;
    if (argc == 0) {
        *out_args = NULL;
        return 1;
    }
    PSValue *vals = NULL;
    if (stack_buf && argc <= stack_cap) {
        vals = stack_buf;
    } else {
        vals = (PSValue *)malloc(argc * sizeof(PSValue));
        if (out_heap) *out_heap = 1;
    }
    if (!vals) return 0;
    for (size_t i = 0; i < argc; i++) {
        PSAstNode *arg = args[i];
        if (arg && arg->kind == AST_LITERAL) {
            if (vm) {
                vm->current_node = arg;
#if PS_ENABLE_PERF
                vm->perf.eval_expr_count++;
                vm->perf.ast_counts[AST_LITERAL]++;
#endif
            }
            vals[i] = arg->as.literal.value;
            continue;
        }
        if (arg && arg->kind == AST_IDENTIFIER) {
            if (vm) {
                vm->current_node = arg;
#if PS_ENABLE_PERF
                vm->perf.eval_expr_count++;
                vm->perf.ast_counts[AST_IDENTIFIER]++;
#endif
            }
            PSString *name = ps_identifier_string(arg);
            int found = 0;
            PSValue v = ps_value_undefined();
            if (!ps_identifier_cached_get(env, arg, &v, &found)) {
                v = ps_env_get(env, name, &found);
            }
            if (!found) {
                ctl->did_throw = 1;
                const char *prefix = "Identifier not defined: ";
                size_t prefix_len = strlen(prefix);
                size_t name_len = name ? name->byte_len : 0;
                char *msg = NULL;
                if (name && name->utf8 && name_len > 0) {
                    msg = (char *)malloc(prefix_len + name_len + 1);
                }
                if (msg) {
                    memcpy(msg, prefix, prefix_len);
                    memcpy(msg + prefix_len, name->utf8, name_len);
                    msg[prefix_len + name_len] = '\0';
                    ctl->throw_value = ps_vm_make_error(vm, "ReferenceError", msg);
                    free(msg);
                } else {
                    ctl->throw_value = ps_vm_make_error(vm, "ReferenceError", "Identifier not defined");
                }
                if (out_heap && *out_heap) {
                    free(vals);
                }
                return 0;
            }
            vals[i] = v;
            continue;
        }
        vals[i] = eval_expression(vm, env, arg, ctl);
        if (ctl->did_throw) {
            if (out_heap && *out_heap) {
                free(vals);
            }
            return 0;
        }
    }
    *out_args = vals;
    return 1;
}

static int ps_eval_math_intrinsic(PSVM *vm,
                                  PSEnv *env,
                                  PSAstNode *call,
                                  PSEvalControl *ctl,
                                  PSValue *out_value) {
    if (!vm || !env || !call || call->kind != AST_CALL) return 0;
    if (!vm->math_obj || !vm->math_intrinsics_valid) return 0;
    PSAstNode *callee = call->as.call.callee;
    if (!callee || callee->kind != AST_MEMBER) return 0;
    PSAstNode *member = callee;
    if (member->as.member.computed) return 0;
    PSAstNode *obj_node = member->as.member.object;
    if (!obj_node || obj_node->kind != AST_IDENTIFIER) return 0;
    PSString *obj_name = ps_identifier_string(obj_node);
    if (!ps_string_equals_cstr(obj_name, "Math")) return 0;

    int found = 0;
    PSValue math_val = ps_value_undefined();
    if (!ps_identifier_cached_get(env, obj_node, &math_val, &found)) {
        math_val = ps_env_get(env, obj_name, &found);
    }
    if (!found || math_val.type != PS_T_OBJECT || math_val.as.object != vm->math_obj) return 0;

    PSAstNode *prop_node = member->as.member.property;
    if (!prop_node || prop_node->kind != AST_IDENTIFIER) return 0;
    PSString *prop = ps_identifier_string(prop_node);
    if (!prop) return 0;

    size_t argc = call->as.call.argc;
    PSAstNode **args = call->as.call.args;

    if (ps_string_equals_cstr(prop, "sqrt")) {
        PSValue arg0 = ps_value_undefined();
        if (argc > 0) {
            arg0 = eval_expression(vm, env, args[0], ctl);
            if (ctl->did_throw) return -1;
        }
        double x = ps_to_number_fast(vm, arg0);
        if (ps_check_pending_throw(vm, ctl)) return -1;
        if (out_value) *out_value = ps_value_number(sqrt(x));
        return 1;
    }
    if (ps_string_equals_cstr(prop, "abs")) {
        PSValue arg0 = ps_value_undefined();
        if (argc > 0) {
            arg0 = eval_expression(vm, env, args[0], ctl);
            if (ctl->did_throw) return -1;
        }
        double x = ps_to_number_fast(vm, arg0);
        if (ps_check_pending_throw(vm, ctl)) return -1;
        if (out_value) *out_value = ps_value_number(fabs(x));
        return 1;
    }
    if (ps_string_equals_cstr(prop, "min")) {
        if (argc == 0) {
            if (out_value) *out_value = ps_value_number(INFINITY);
            return 1;
        }
        double minv = INFINITY;
        for (size_t i = 0; i < argc; i++) {
            PSValue v = eval_expression(vm, env, args[i], ctl);
            if (ctl->did_throw) return -1;
            double num = ps_to_number_fast(vm, v);
            if (ps_check_pending_throw(vm, ctl)) return -1;
            if (ps_value_is_nan(num)) {
                if (out_value) *out_value = ps_value_number(0.0 / 0.0);
                return 1;
            }
            if (num < minv) minv = num;
        }
        if (out_value) *out_value = ps_value_number(minv);
        return 1;
    }
    if (ps_string_equals_cstr(prop, "max")) {
        if (argc == 0) {
            if (out_value) *out_value = ps_value_number(-INFINITY);
            return 1;
        }
        double maxv = -INFINITY;
        for (size_t i = 0; i < argc; i++) {
            PSValue v = eval_expression(vm, env, args[i], ctl);
            if (ctl->did_throw) return -1;
            double num = ps_to_number_fast(vm, v);
            if (ps_check_pending_throw(vm, ctl)) return -1;
            if (ps_value_is_nan(num)) {
                if (out_value) *out_value = ps_value_number(0.0 / 0.0);
                return 1;
            }
            if (num > maxv) maxv = num;
        }
        if (out_value) *out_value = ps_value_number(maxv);
        return 1;
    }
    if (ps_string_equals_cstr(prop, "pow")) {
        PSValue arg0 = ps_value_undefined();
        PSValue arg1 = ps_value_undefined();
        if (argc > 0) {
            arg0 = eval_expression(vm, env, args[0], ctl);
            if (ctl->did_throw) return -1;
        }
        if (argc > 1) {
            arg1 = eval_expression(vm, env, args[1], ctl);
            if (ctl->did_throw) return -1;
        }
        double x = ps_to_number_fast(vm, arg0);
        if (ps_check_pending_throw(vm, ctl)) return -1;
        double y = ps_to_number_fast(vm, arg1);
        if (ps_check_pending_throw(vm, ctl)) return -1;
        if (out_value) *out_value = ps_value_number(pow(x, y));
        return 1;
    }
    if (ps_string_equals_cstr(prop, "floor")) {
        PSValue arg0 = ps_value_undefined();
        if (argc > 0) {
            arg0 = eval_expression(vm, env, args[0], ctl);
            if (ctl->did_throw) return -1;
        }
        double x = ps_to_number_fast(vm, arg0);
        if (ps_check_pending_throw(vm, ctl)) return -1;
        if (out_value) *out_value = ps_value_number(floor(x));
        return 1;
    }
    if (ps_string_equals_cstr(prop, "ceil")) {
        PSValue arg0 = ps_value_undefined();
        if (argc > 0) {
            arg0 = eval_expression(vm, env, args[0], ctl);
            if (ctl->did_throw) return -1;
        }
        double x = ps_to_number_fast(vm, arg0);
        if (ps_check_pending_throw(vm, ctl)) return -1;
        if (out_value) *out_value = ps_value_number(ceil(x));
        return 1;
    }
    if (ps_string_equals_cstr(prop, "round")) {
        PSValue arg0 = ps_value_undefined();
        if (argc > 0) {
            arg0 = eval_expression(vm, env, args[0], ctl);
            if (ctl->did_throw) return -1;
        }
        double x = ps_to_number_fast(vm, arg0);
        if (ps_check_pending_throw(vm, ctl)) return -1;
        if (ps_value_is_nan(x) || isinf(x)) {
            if (out_value) *out_value = ps_value_number(x);
            return 1;
        }
        double r = floor(x + 0.5);
        if (r == 0.0 && x < 0.0) r = -0.0;
        if (out_value) *out_value = ps_value_number(r);
        return 1;
    }

    return 0;
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

static int ps_env_prepare_fast(PSEnv *env, PSString **names, size_t count) {
    if (!env) return 0;
    size_t prev_count = env->fast_count;
    env->fast_names = names;
    env->fast_count = count;
    if (count == 0) {
        free(env->fast_values);
        env->fast_values = NULL;
        return 1;
    }
    if (!env->fast_values || prev_count != count) {
        PSValue *vals = (PSValue *)realloc(env->fast_values, sizeof(PSValue) * count);
        if (!vals) return 0;
        env->fast_values = vals;
    }
    memset(env->fast_values, 0, sizeof(PSValue) * count);
    return 1;
}

static int ps_env_prepare_fast_partial(PSEnv *env,
                                       PSString **names,
                                       size_t count,
                                       size_t *clear_idx,
                                       size_t clear_count) {
    if (!env) return 0;
    size_t prev_count = env->fast_count;
    env->fast_names = names;
    env->fast_count = count;
    if (count == 0) {
        free(env->fast_values);
        env->fast_values = NULL;
        return 1;
    }
    if (!env->fast_values || prev_count != count) {
        PSValue *vals = (PSValue *)realloc(env->fast_values, sizeof(PSValue) * count);
        if (!vals) return 0;
        env->fast_values = vals;
        memset(env->fast_values, 0, sizeof(PSValue) * count);
        return 1;
    }
    if (clear_idx && clear_count) {
        for (size_t i = 0; i < clear_count; i++) {
            size_t idx = clear_idx[i];
            if (idx < count) {
                env->fast_values[idx] = ps_value_undefined();
            }
        }
        return 1;
    }
    memset(env->fast_values, 0, sizeof(PSValue) * count);
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
        if (throw_value) {
            const char *kind = ps_object_kind_label(fn_obj);
            char msg[96];
            snprintf(msg, sizeof(msg), "Not a callable object: %s", kind);
            *throw_value = ps_vm_make_error(vm, "TypeError", msg);
        }
        return throw_value ? *throw_value : ps_value_undefined();
    }

    PSFunction *func = ps_function_from_object(fn_obj);
    if (!func) {
        if (did_throw) *did_throw = 1;
        if (throw_value) {
            const char *kind = ps_object_kind_label(fn_obj);
            char msg[96];
            snprintf(msg, sizeof(msg), "Not a callable object: %s", kind);
            *throw_value = ps_vm_make_error(vm, "TypeError", msg);
        }
        return throw_value ? *throw_value : ps_value_undefined();
    }
#if PS_ENABLE_PERF
    if (vm) {
        vm->perf.call_count++;
        if (func->is_native) {
            vm->perf.native_call_count++;
        }
    }
#endif

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
        size_t prev_depth = vm ? vm->stack_depth : 0;
        int pushed = 0;
        if (vm) {
            PSFunction *prev_func = prev ? ps_function_from_object(prev) : NULL;
            ps_vm_push_frame(vm, prev_func);
            if (vm->stack_depth > prev_depth) {
                pushed = 1;
            }
            vm->current_callee = fn_obj;
        }
        PSValue result = func->native(vm, this_val, argc, argv);
        if (vm) {
            vm->current_callee = prev;
            if (pushed) {
                ps_vm_pop_frame(vm);
            }
        }
        if (vm && vm->has_pending_throw) {
            if (did_throw) *did_throw = 1;
            if (throw_value) *throw_value = vm->pending_throw;
            vm->has_pending_throw = 0;
            return throw_value ? *throw_value : ps_value_undefined();
        }
        return result;
    }

#if PS_ENABLE_FAST_CALLS
    if (func->param_count == 2 && argc >= 2) {
        if (ps_fast_add_applicable(func, NULL, NULL)) {
            PSValue a = argv[0];
            PSValue b = argv[1];
            if (a.type == PS_T_NUMBER && b.type == PS_T_NUMBER) {
                return ps_value_number(a.as.number + b.as.number);
            }
        }
    }
    if (argc >= 1 && ps_fast_fib_applicable(func)) {
        PSValue n_val = argv[0];
        if (n_val.type == PS_T_NUMBER) {
            double n = n_val.as.number;
            if (isfinite(n) && n >= 0.0 && floor(n) == n) {
                return ps_value_number(ps_fast_fib_value((uint64_t)n));
            }
        }
    }
#endif

    ps_fast_math_prepare(func);
    if ((func->fast_flags & PS_FAST_FLAG_MATH) && func->fast_math_expr) {
        double out = 0.0;
        if (ps_fast_math_eval(func, func->fast_math_expr, argc, argv, &out)) {
            return ps_value_number(out);
        }
    }

    ps_fast_clamp_prepare(func);
    if (func->fast_flags & PS_FAST_FLAG_CLAMP) {
        if (argc >= 1 && argv && argv[0].type == PS_T_NUMBER) {
            double num = argv[0].as.number;
            if (ps_value_is_nan(num)) {
                return ps_value_number(0.0 / 0.0);
            }
            if (num < func->fast_clamp_min) return ps_value_number(func->fast_clamp_min);
            if (num > func->fast_clamp_max) return ps_value_number(func->fast_clamp_max);
            if (func->fast_clamp_use_floor) {
                num = floor(num);
            }
            return ps_value_number(num);
        }
    }

    ps_fast_num_prepare(func);
    if (func->fast_flags & PS_FAST_FLAG_NUM) {
        double out = 0.0;
        if (ps_fast_num_eval(vm, func, argc, argv, &out)) {
            return ps_value_number(out);
        }
    }

    ps_fast_env_prepare(func);
    int use_fast_env = (func->fast_flags & PS_FAST_FLAG_ENV) != 0;

    PSEnv *call_env = NULL;
    int fast_env_cached = 0;
    if (use_fast_env) {
        if (func->fast_env && !func->fast_env_in_use) {
            call_env = func->fast_env;
            func->fast_env_in_use = 1;
            fast_env_cached = 1;
        } else {
            call_env = ps_env_new(func->env ? func->env : env, NULL, 0);
            if (call_env && func->env && !func->fast_env) {
                func->fast_env = call_env;
                func->fast_env_in_use = 1;
                fast_env_cached = 1;
            }
        }
    }
    if (!call_env) {
        call_env = ps_env_new_object(func->env ? func->env : env);
    }
    if (!call_env) return ps_value_undefined();
    if (vm && vm->object_proto && call_env->record) {
        call_env->record->prototype = vm->object_proto;
    }
    int fast_env_active = use_fast_env && call_env->record == NULL;

    PSObject *prev_callee = vm ? vm->current_callee : NULL;
    size_t prev_depth = vm ? vm->stack_depth : 0;
    int pushed = 0;
    if (vm) {
        PSFunction *prev_func = prev_callee ? ps_function_from_object(prev_callee) : NULL;
        ps_vm_push_frame(vm, prev_func);
        if (vm->stack_depth > prev_depth) {
            pushed = 1;
        }
        vm->current_callee = fn_obj;
    }

    if (fast_env_active) {
        if (!ps_env_prepare_fast_partial(call_env,
                                         func->fast_names,
                                         func->fast_count,
                                         func->fast_local_index,
                                         func->fast_local_index_count)) {
            if (fast_env_cached) {
                func->fast_env_in_use = 0;
            }
            return ps_value_undefined();
        }
        if (call_env->fast_values && func->fast_this_index < call_env->fast_count) {
            call_env->fast_values[func->fast_this_index] = this_val;
        }
    } else {
        PSString **fast_names = func->fast_names ? func->fast_names : func->param_names;
        size_t fast_count = func->fast_names ? func->fast_count : func->param_count;
        if (!ps_env_prepare_fast(call_env, fast_names, fast_count)) {
            if (fast_env_cached) {
                func->fast_env_in_use = 0;
            }
            return ps_value_undefined();
        }
        ps_object_define(call_env->record,
                         ps_string_from_cstr("this"),
                         this_val,
                         PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
        if (call_env->fast_values && func->fast_names &&
            func->fast_this_index < call_env->fast_count) {
            call_env->fast_values[func->fast_this_index] = this_val;
        }
    }

    call_env->callee_obj = fn_obj;
    call_env->arguments_values = argv;
    call_env->arguments_count = (size_t)argc;
    call_env->arguments_obj = NULL;

    if (!fast_env_active) {
        ps_fast_hoist_prepare(func);
        if (func->fast_flags & PS_FAST_FLAG_HOIST) {
            hoist_decls(vm, call_env, func->body);
        }
    }

    if (fast_env_active) {
        if (call_env->fast_values && func->fast_param_index) {
            for (size_t i = 0; i < func->param_count; i++) {
                size_t idx = func->fast_param_index[i];
                if (idx == SIZE_MAX || idx >= call_env->fast_count) continue;
                call_env->fast_values[idx] = ((int)i < argc) ? argv[i] : ps_value_undefined();
            }
        }
    } else {
        for (size_t i = 0; i < func->param_count; i++) {
            PSAstNode *param = func->params[i];
            PSString *name = func->param_names ? func->param_names[i] : NULL;
            if (!name && param && param->kind == AST_IDENTIFIER) {
                name = ps_identifier_string(param);
            }
            if (!name) continue;
            PSValue val = ((int)i < argc) ? argv[i] : ps_value_undefined();
            ps_env_define(call_env, name, val);
        }
    }

#if PS_ENABLE_ARGUMENTS_ALIASING
    call_env->param_names = func->param_names;
    call_env->param_count = func->param_count;
    call_env->param_names_owned = 0;
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
            if (vm) {
                vm->current_callee = prev_callee;
                if (pushed) {
                    ps_vm_pop_frame(vm);
                }
            }
            if (fast_env_cached) {
                func->fast_env_in_use = 0;
            }
            if (did_throw) *did_throw = 1;
            if (throw_value) *throw_value = default_ctl.throw_value;
            return throw_value ? *throw_value : ps_value_undefined();
        }
        PSAstNode *param = func->params[i];
        PSString *name = func->param_names ? func->param_names[i] : NULL;
        if (!name && param && param->kind == AST_IDENTIFIER) {
            name = ps_identifier_string(param);
        }
        if (!name) continue;
        ps_env_set(call_env, name, default_val);
    }

    PSEvalControl inner = {0};
    PSEnv *prev_env = vm ? vm->env : NULL;
    size_t root_count = 0;
    if (vm && prev_env) {
        ps_gc_root_push(vm, PS_GC_ROOT_ENV, prev_env);
        root_count = 1;
    }
    if (vm) vm->env = call_env;
    PSValue ret = ps_value_undefined();
    if (func->stmt_bc_state == 0) {
        PSStmtBC *bc = ps_stmt_bc_compile(func->body);
        if (bc) {
            func->stmt_bc = bc;
            func->stmt_bc_state = 1;
        } else {
            func->stmt_bc_state = 2;
        }
    }
    if (func->stmt_bc_state == 1 && func->stmt_bc) {
        ret = ps_stmt_bc_execute(vm, call_env, func, func->stmt_bc, &inner);
    } else {
        ret = eval_node(vm, call_env, func->body, &inner);
    }
    if (vm) vm->env = prev_env;
    if (vm && root_count) {
        ps_gc_root_pop(vm, root_count);
    }
    ps_env_free(call_env);
    if (vm) {
        vm->current_callee = prev_callee;
        if (pushed) {
            ps_vm_pop_frame(vm);
        }
    }
    if (fast_env_cached) {
        func->fast_env_in_use = 0;
    }
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
            PSString *name = ps_identifier_string(node->as.var_decl.id);
            int found = 0;
            (void)ps_object_get_own(env->record, name, &found);
            if (!found) {
                ps_env_define(env, name, ps_value_undefined());
            }
            break;
        }
        case AST_FUNCTION_DECL: {
            PSString *name = ps_identifier_string(node->as.func_decl.id);
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
                PSString *name = ps_identifier_string(node->as.for_in.target);
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
                PSString *name = ps_identifier_string(node->as.for_of.target);
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

    if (vm) {
        vm->current_ast = program;
        vm->root_ast = program;
    }
    hoist_decls(vm, vm->env, program);

    for (size_t i = 0; i < program->as.list.count; i++) {
        last = eval_node(vm, vm->env, program->as.list.items[i], &ctl);
        if (ctl.did_throw) {
            ps_print_uncaught(vm, ctl.throw_value);
            exit(1);
        }
        if (ctl.did_return) {
            if (vm) {
                vm->current_ast = NULL;
                vm->root_ast = NULL;
                vm->current_node = NULL;
            }
            return last;
        }
    }
    if (vm) {
        vm->current_ast = NULL;
        vm->root_ast = NULL;
        vm->current_node = NULL;
    }
    return last;
}

/* --------------------------------------------------------- */
/* Statements                                                */
/* --------------------------------------------------------- */

static PSValue eval_node(PSVM *vm, PSEnv *env, PSAstNode *node, PSEvalControl *ctl) {
    if (vm) {
        vm->env = env;
        vm->current_node = node;
#if PS_ENABLE_PERF
        vm->perf.eval_node_count++;
        if (node && node->kind < PS_AST_KIND_COUNT) {
            vm->perf.ast_counts[node->kind]++;
        }
#endif
        ps_gc_safe_point(vm);
        if (ps_eval_trace_enabled != 0) {
            ps_eval_trace_poll(vm, node);
        }
    }
    switch (node->kind) {
        case AST_BLOCK:
            return eval_block(vm, env, node, ctl);

        case AST_VAR_DECL: {
            PSString *name = ps_identifier_string(node->as.var_decl.id);

            PSValue val = ps_value_undefined();
            if (node->as.var_decl.init) {
                val = eval_expression(vm, env, node->as.var_decl.init, ctl);
            }

            PSEnv *target_env = env;
            while (target_env && target_env->is_with) {
                target_env = target_env->parent;
            }
            if (!target_env) {
                target_env = env;
            }
            if (node->as.var_decl.init) {
                (void)ps_env_set(target_env, name, val);
            } else {
                int found = 0;
                if (target_env->record) {
                    (void)ps_object_get_own(target_env->record, name, &found);
                }
                if (!found) {
                    (void)ps_env_define(target_env, name, ps_value_undefined());
                }
            }
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
                loop_label = ps_identifier_string(node->as.while_stmt.label);
            }
            PSValue last = ps_value_undefined();
            while (1) {
                PS_PERF_POLL(vm);
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
                loop_label = ps_identifier_string(node->as.do_while.label);
            }
            PSValue last = ps_value_undefined();
            do {
                PS_PERF_POLL(vm);
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
                loop_label = ps_identifier_string(node->as.for_stmt.label);
            }
            PSValue last = ps_value_undefined();
            if (node->as.for_stmt.init) {
                last = eval_node(vm, env, node->as.for_stmt.init, ctl);
                if (ctl->did_throw || ctl->did_return) return last;
            }
            while (1) {
                PS_PERF_POLL(vm);
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
                loop_label = ps_identifier_string(node->as.for_in.label);
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
                        if (ps_array_string_to_index(name, &idx)) {
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
                PS_PERF_POLL(vm);
                PSValue name_val = ps_value_string(ordered[i]);
                if (node->as.for_in.is_var) {
                    if (node->as.for_in.target->kind == AST_IDENTIFIER) {
                        PSString *name = ps_identifier_string(node->as.for_in.target);
                        ps_env_define(env, name, name_val);
                    }
                } else if (node->as.for_in.target->kind == AST_IDENTIFIER) {
                    PSString *name = ps_identifier_string(node->as.for_in.target);
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
                loop_label = ps_identifier_string(node->as.for_of.label);
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
                    PS_PERF_POLL(vm);
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
                    PS_PERF_POLL(vm);
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
                PS_PERF_POLL(vm);
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
                switch_label = ps_identifier_string(node->as.switch_stmt.label);
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
                ctl->break_label = ps_identifier_string(node->as.jump_stmt.label);
            } else {
                ctl->break_label = NULL;
            }
            ctl->did_break = 1;
            return ps_value_undefined();

        case AST_CONTINUE:
            if (node->as.jump_stmt.label) {
                ctl->continue_label = ps_identifier_string(node->as.jump_stmt.label);
            } else {
                ctl->continue_label = NULL;
            }
            ctl->did_continue = 1;
            return ps_value_undefined();

        case AST_LABEL: {
            PSString *label = ps_identifier_string(node->as.label_stmt.label);
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
            with_env->is_with = 1;
            PSEnv *prev_env = vm ? vm->env : NULL;
            PSValue last = eval_node(vm, with_env, node->as.with_stmt.body, ctl);
            if (vm) vm->env = prev_env;
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
                    PSString *name = ps_identifier_string(node->as.try_stmt.catch_param);
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
/* Expression bytecode                                       */
/* --------------------------------------------------------- */

typedef struct {
    PSExprBCInstr *code;
    size_t count;
    size_t cap;
    PSValue *consts;
    size_t const_count;
    size_t const_cap;
} PSExprBCBuilder;

enum {
    PS_EXPR_BC_PUSH_CONST = 1,
    PS_EXPR_BC_LOAD_IDENT,
    PS_EXPR_BC_LOAD_THIS,
    PS_EXPR_BC_UNARY_NUM,
    PS_EXPR_BC_BINARY_NUM,
    PS_EXPR_BC_GET_MEMBER,
    PS_EXPR_BC_CALL_IDENT,
    PS_EXPR_BC_STORE_IDENT,
    PS_EXPR_BC_STORE_MEMBER,
    PS_EXPR_BC_EVAL_AST
};

static int ps_expr_bc_push_instr(PSExprBCBuilder *b, PSExprBCInstr instr) {
    if (!b) return 0;
    if (b->count == b->cap) {
        size_t new_cap = b->cap ? b->cap * 2 : 32;
        PSExprBCInstr *next = (PSExprBCInstr *)realloc(b->code, new_cap * sizeof(PSExprBCInstr));
        if (!next) return 0;
        b->code = next;
        b->cap = new_cap;
    }
    b->code[b->count++] = instr;
    return 1;
}

static int ps_expr_bc_add_const(PSExprBCBuilder *b, PSValue value, int *out_index) {
    if (!b) return 0;
    if (b->const_count == b->const_cap) {
        size_t new_cap = b->const_cap ? b->const_cap * 2 : 16;
        PSValue *next = (PSValue *)realloc(b->consts, new_cap * sizeof(PSValue));
        if (!next) return 0;
        b->consts = next;
        b->const_cap = new_cap;
    }
    int idx = (int)b->const_count;
    b->consts[b->const_count++] = value;
    if (out_index) *out_index = idx;
    return 1;
}

static int ps_expr_bc_emit(PSExprBCBuilder *b, PSAstNode *expr);

static int ps_expr_bc_emit_member(PSExprBCBuilder *b, PSAstNode *expr) {
    PSAstNode *obj = expr->as.member.object;
    PSAstNode *prop = expr->as.member.property;
    if (!obj) return 0;
    if (!ps_expr_bc_emit(b, obj)) return 0;
    if (expr->as.member.computed) {
        if (!prop) return 0;
        if (!ps_expr_bc_emit(b, prop)) return 0;
    } else {
        PSValue prop_val = ps_value_undefined();
        if (prop && prop->kind == AST_IDENTIFIER) {
            PSString *name = ps_identifier_string(prop);
            prop_val = ps_value_string(name);
        } else if (prop && prop->kind == AST_LITERAL &&
                   prop->as.literal.value.type == PS_T_STRING) {
            prop_val = prop->as.literal.value;
        } else {
            return 0;
        }
        int cidx = 0;
        if (!ps_expr_bc_add_const(b, prop_val, &cidx)) return 0;
        PSExprBCInstr push = {0};
        push.op = PS_EXPR_BC_PUSH_CONST;
        push.a = cidx;
        if (!ps_expr_bc_push_instr(b, push)) return 0;
    }
    PSExprBCInstr instr = {0};
    instr.op = PS_EXPR_BC_GET_MEMBER;
    instr.a = expr->as.member.computed ? 1 : 0;
    instr.ptr = expr;
    return ps_expr_bc_push_instr(b, instr);
}

static int ps_expr_bc_emit_call_ident(PSExprBCBuilder *b, PSAstNode *expr) {
    if (!expr || expr->kind != AST_CALL) return 0;
    PSAstNode *callee = expr->as.call.callee;
    if (!callee || callee->kind != AST_IDENTIFIER) return 0;
    if (ps_string_equals_cstr(ps_identifier_string(callee), "eval")) return 0;
    for (size_t i = 0; i < expr->as.call.argc; i++) {
        if (!ps_expr_bc_emit(b, expr->as.call.args[i])) return 0;
    }
    PSExprBCInstr instr = {0};
    instr.op = PS_EXPR_BC_CALL_IDENT;
    instr.a = (int)expr->as.call.argc;
    instr.ptr = callee;
    return ps_expr_bc_push_instr(b, instr);
}

static int ps_expr_bc_emit_assign(PSExprBCBuilder *b, PSAstNode *expr) {
    if (!expr || expr->kind != AST_ASSIGN) return 0;
    if (expr->as.assign.op != TOK_ASSIGN) return 0;
    PSAstNode *target = expr->as.assign.target;
    PSAstNode *value = expr->as.assign.value;
    if (!target || !value) return 0;
    if (!ps_expr_bc_emit(b, value)) return 0;
    if (target->kind == AST_IDENTIFIER) {
        PSExprBCInstr instr = {0};
        instr.op = PS_EXPR_BC_STORE_IDENT;
        instr.ptr = target;
        return ps_expr_bc_push_instr(b, instr);
    }
    if (target->kind == AST_MEMBER) {
        PSAstNode *member = target;
        PSAstNode *obj = member->as.member.object;
        PSAstNode *prop = member->as.member.property;
        if (!obj) return 0;
        if (!ps_expr_bc_emit(b, obj)) return 0;
        if (member->as.member.computed) {
            if (!prop) return 0;
            if (!ps_expr_bc_emit(b, prop)) return 0;
        } else {
            PSValue prop_val = ps_value_undefined();
            if (prop && prop->kind == AST_IDENTIFIER) {
                PSString *name = ps_identifier_string(prop);
                prop_val = ps_value_string(name);
            } else if (prop && prop->kind == AST_LITERAL &&
                       prop->as.literal.value.type == PS_T_STRING) {
                prop_val = prop->as.literal.value;
            } else {
                return 0;
            }
            int cidx = 0;
            if (!ps_expr_bc_add_const(b, prop_val, &cidx)) return 0;
            PSExprBCInstr push = {0};
            push.op = PS_EXPR_BC_PUSH_CONST;
            push.a = cidx;
            if (!ps_expr_bc_push_instr(b, push)) return 0;
        }
        PSExprBCInstr instr = {0};
        instr.op = PS_EXPR_BC_STORE_MEMBER;
        instr.a = member->as.member.computed ? 1 : 0;
        instr.ptr = member;
        return ps_expr_bc_push_instr(b, instr);
    }
    return 0;
}

static int ps_expr_bc_emit(PSExprBCBuilder *b, PSAstNode *expr) {
    if (!b || !expr) return 0;
    switch (expr->kind) {
        case AST_LITERAL: {
            int cidx = 0;
            if (!ps_expr_bc_add_const(b, expr->as.literal.value, &cidx)) return 0;
            PSExprBCInstr instr = {0};
            instr.op = PS_EXPR_BC_PUSH_CONST;
            instr.a = cidx;
            return ps_expr_bc_push_instr(b, instr);
        }
        case AST_IDENTIFIER: {
            PSExprBCInstr instr = {0};
            instr.op = PS_EXPR_BC_LOAD_IDENT;
            instr.ptr = expr;
            return ps_expr_bc_push_instr(b, instr);
        }
        case AST_THIS: {
            PSExprBCInstr instr = {0};
            instr.op = PS_EXPR_BC_LOAD_THIS;
            return ps_expr_bc_push_instr(b, instr);
        }
        case AST_UNARY: {
            if (expr->as.unary.op != TOK_PLUS && expr->as.unary.op != TOK_MINUS) return 0;
            if (!ps_expr_bc_emit(b, expr->as.unary.expr)) return 0;
            PSExprBCInstr instr = {0};
            instr.op = PS_EXPR_BC_UNARY_NUM;
            instr.a = expr->as.unary.op;
            return ps_expr_bc_push_instr(b, instr);
        }
        case AST_BINARY: {
            int op = expr->as.binary.op;
            if (op == TOK_AND_AND || op == TOK_OR_OR || op == TOK_COMMA) return 0;
            if (!ps_expr_bc_emit(b, expr->as.binary.left)) return 0;
            if (!ps_expr_bc_emit(b, expr->as.binary.right)) return 0;
            PSExprBCInstr instr = {0};
            instr.op = PS_EXPR_BC_BINARY_NUM;
            instr.a = op;
            return ps_expr_bc_push_instr(b, instr);
        }
        case AST_MEMBER:
            return ps_expr_bc_emit_member(b, expr);
        case AST_CALL: {
            if (ps_expr_bc_emit_call_ident(b, expr)) return 1;
            break;
        }
        case AST_ASSIGN:
            return ps_expr_bc_emit_assign(b, expr);
        default:
            break;
    }
    PSExprBCInstr fallback = {0};
    fallback.op = PS_EXPR_BC_EVAL_AST;
    fallback.ptr = expr;
    return ps_expr_bc_push_instr(b, fallback);
}

static PSExprBC *ps_expr_bc_compile(PSAstNode *expr) {
    if (!expr) return NULL;
    PSExprBCBuilder builder = {0};
    if (!ps_expr_bc_emit(&builder, expr)) {
        free(builder.code);
        free(builder.consts);
        return NULL;
    }
    PSExprBC *bc = (PSExprBC *)calloc(1, sizeof(PSExprBC));
    if (!bc) {
        free(builder.code);
        free(builder.consts);
        return NULL;
    }
    bc->code = builder.code;
    bc->count = builder.count;
    bc->consts = builder.consts;
    bc->const_count = builder.const_count;
    return bc;
}

static PSValue ps_expr_bc_execute(PSVM *vm,
                                  PSEnv *env,
                                  PSAstNode *root,
                                  PSExprBC *bc,
                                  PSEvalControl *ctl,
                                  int *out_ok) {
    if (out_ok) *out_ok = 0;
    if (!bc || !bc->code || bc->count == 0) return ps_value_undefined();
    PSValue stack_buf[64];
    PSValue *stack = stack_buf;
    size_t sp = 0;
    if (bc->count > 64) {
        stack = (PSValue *)calloc(bc->count, sizeof(PSValue));
        if (!stack) return ps_value_undefined();
    }
    if (vm) vm->current_node = root;
    for (size_t ip = 0; ip < bc->count; ip++) {
        PSExprBCInstr *ins = &bc->code[ip];
        switch (ins->op) {
            case PS_EXPR_BC_PUSH_CONST:
                if (ins->a < 0 || (size_t)ins->a >= bc->const_count) goto fail;
                stack[sp++] = bc->consts[ins->a];
                break;
            case PS_EXPR_BC_LOAD_IDENT: {
                PSAstNode *id = (PSAstNode *)ins->ptr;
                int found = 0;
                PSValue v = ps_value_undefined();
                if (!ps_identifier_cached_get(env, id, &v, &found)) {
                    PSString *name = ps_identifier_string(id);
                    v = ps_env_get(env, name, &found);
                }
                if (!found) {
                    ctl->did_throw = 1;
                    const char *prefix = "Identifier not defined: ";
                    PSString *name = ps_identifier_string(id);
                    size_t prefix_len = strlen(prefix);
                    size_t name_len = name ? name->byte_len : 0;
                    char *msg = NULL;
                    if (name && name->utf8 && name_len > 0) {
                        msg = (char *)malloc(prefix_len + name_len + 1);
                    }
                    if (msg) {
                        memcpy(msg, prefix, prefix_len);
                        memcpy(msg + prefix_len, name->utf8, name_len);
                        msg[prefix_len + name_len] = '\0';
                        ctl->throw_value = ps_vm_make_error(vm, "ReferenceError", msg);
                        free(msg);
                    } else {
                        ctl->throw_value = ps_vm_make_error(vm, "ReferenceError", "Identifier not defined");
                    }
                    goto fail;
                }
                stack[sp++] = v;
                break;
            }
            case PS_EXPR_BC_LOAD_THIS: {
                PSString *name = ps_string_from_cstr("this");
                int found = 0;
                PSValue v = ps_env_get(env, name, &found);
                if (found) {
                    stack[sp++] = v;
                } else if (vm && vm->global) {
                    stack[sp++] = ps_value_object(vm->global);
                } else {
                    stack[sp++] = ps_value_undefined();
                }
                break;
            }
            case PS_EXPR_BC_UNARY_NUM: {
                if (sp == 0) goto fail;
                PSValue v = stack[sp - 1];
                if (v.type != PS_T_NUMBER) goto fallback;
                if (ins->a == TOK_MINUS) {
                    v.as.number = -v.as.number;
                }
                stack[sp - 1] = v;
                break;
            }
            case PS_EXPR_BC_BINARY_NUM: {
                if (sp < 2) goto fail;
                PSValue right = stack[--sp];
                PSValue left = stack[--sp];
                if (ins->a == TOK_AND_AND || ins->a == TOK_OR_OR || ins->a == TOK_COMMA) {
                    goto fallback;
                }
                PSValue out = ps_eval_binary_values(vm, ins->a, left, right, ctl);
                if (ctl->did_throw) goto fail;
                stack[sp++] = out;
                break;
            }
            case PS_EXPR_BC_GET_MEMBER: {
                if (sp < 2) goto fail;
                PSValue key_val = stack[--sp];
                PSValue obj_val = stack[--sp];
                PSAstNode *member = (PSAstNode *)ins->ptr;
                PSObject *obj = ps_to_object(vm, &obj_val, ctl);
                if (ctl->did_throw) goto fail;
                if (obj && obj->kind == PS_OBJ_KIND_BUFFER && ins->a) {
                    PSValue out = ps_value_undefined();
                    int handled = ps_buffer_read_index_value(vm, obj, key_val, &out, ctl);
                    if (handled < 0) goto fail;
                    if (handled > 0) {
                        stack[sp++] = out;
                        break;
                    }
                    PSString *prop = ps_to_string(vm, key_val);
                    if (ps_check_pending_throw(vm, ctl)) goto fail;
                    handled = ps_buffer_read_index(vm, obj, prop, &out, ctl);
                    if (handled < 0) goto fail;
                    if (handled > 0) {
                        stack[sp++] = out;
                        break;
                    }
                    int found;
                    stack[sp++] = ps_object_get(obj, prop, &found);
                    break;
                } else if (obj && obj->kind == PS_OBJ_KIND_BUFFER32 && ins->a) {
                    PSValue out = ps_value_undefined();
                    int handled = ps_buffer32_read_index_value(vm, obj, key_val, &out, ctl);
                    if (handled < 0) goto fail;
                    if (handled > 0) {
                        stack[sp++] = out;
                        break;
                    }
                    PSString *prop = ps_to_string(vm, key_val);
                    if (ps_check_pending_throw(vm, ctl)) goto fail;
                    handled = ps_buffer32_read_index(vm, obj, prop, &out, ctl);
                    if (handled < 0) goto fail;
                    if (handled > 0) {
                        stack[sp++] = out;
                        break;
                    }
                    int found;
                    stack[sp++] = ps_object_get(obj, prop, &found);
                    break;
                } else if (obj && obj->kind == PS_OBJ_KIND_ARRAY && ins->a) {
                    PSValue out = ps_value_undefined();
                    size_t index = 0;
                    int handled = ps_array_read_index_fast_value(obj, key_val, &out, &index);
                    if (handled > 0) {
                        stack[sp++] = out;
                        break;
                    }
                    if (handled < 0) {
                        PSString *prop = ps_array_index_string(vm, index);
                        int found;
                        stack[sp++] = ps_object_get(obj, prop, &found);
                        break;
                    }
                    handled = ps_array_read_index_value(vm, obj, key_val, &out, ctl);
                    if (handled < 0) goto fail;
                    if (handled > 0) {
                        stack[sp++] = out;
                        break;
                    }
                    PSString *prop = ps_to_string(vm, key_val);
                    if (ps_check_pending_throw(vm, ctl)) goto fail;
                    int found;
                    stack[sp++] = ps_object_get(obj, prop, &found);
                    break;
                } else if (obj && obj->kind == PS_OBJ_KIND_PLAIN && ins->a) {
                    size_t index = 0;
                    int idx = ps_value_to_index(vm, key_val, &index, ctl);
                    if (idx < 0) goto fail;
                    if (idx > 0 && obj->internal_kind == PS_INTERNAL_NUMMAP) {
                        PSValue out = ps_value_undefined();
                        if (ps_num_map_get(obj, index, &out)) {
                            stack[sp++] = out;
                        } else {
                            stack[sp++] = ps_value_undefined();
                        }
                        break;
                    }
                    if (key_val.type == PS_T_STRING && obj->internal_kind == PS_INTERNAL_NUMMAP) {
                        uint32_t kindex = 0;
                        if (ps_string_to_k_index(key_val.as.string, &kindex)) {
                            PSValue out = ps_value_undefined();
                            if (ps_num_map_k_get(obj, kindex, &out)) {
                                stack[sp++] = out;
                                break;
                            }
                        }
                    }
                    PSString *prop = (key_val.type == PS_T_STRING) ? key_val.as.string
                                                                   : ps_to_string(vm, key_val);
                    if (ps_check_pending_throw(vm, ctl)) goto fail;
                    int found;
                    stack[sp++] = ps_object_get(obj, prop, &found);
                    break;
                }
                PSString tmp_prop;
                char tmp_buf[96];
                PSString *prop = NULL;
                if (ins->a) {
                    prop = ps_member_key_read(vm, env, member, ctl, &tmp_prop, tmp_buf, sizeof(tmp_buf));
                } else if (key_val.type == PS_T_STRING) {
                    prop = key_val.as.string;
                } else {
                    prop = ps_to_string(vm, key_val);
                }
                if (ctl->did_throw || ps_check_pending_throw(vm, ctl)) goto fail;
                PSValue out = ps_value_undefined();
                int handled = ps_buffer_read_index(vm, obj, prop, &out, ctl);
                if (handled < 0) goto fail;
                if (handled > 0) {
                    stack[sp++] = out;
                    break;
                }
                if (ps_member_cached_get(obj, member, prop, &out)) {
                    stack[sp++] = out;
                    break;
                }
                {
                    int found;
                    stack[sp++] = ps_object_get(obj, prop, &found);
                }
                break;
            }
            case PS_EXPR_BC_CALL_IDENT: {
                int argc = ins->a;
                PSAstNode *id = (PSAstNode *)ins->ptr;
                PSValue callee = ps_value_undefined();
                int found = 0;
                if (!ps_identifier_cached_get(env, id, &callee, &found)) {
                    PSString *name = ps_identifier_string(id);
                    callee = ps_env_get(env, name, &found);
                }
                if (!found) {
                    ctl->did_throw = 1;
                    ctl->throw_value = ps_vm_make_error(vm, "ReferenceError", "Identifier not defined");
                    goto fail;
                }
                if (callee.type != PS_T_OBJECT) {
                    ctl->did_throw = 1;
                    ctl->throw_value = ps_vm_make_error(vm, "TypeError", "Call of non-object");
                    goto fail;
                }
                PSValue *args = NULL;
                PSValue stack_args[8];
                int args_heap = 0;
                if (argc < 0 || (size_t)argc > sp) goto fail;
                if (argc <= 8) {
                    args = stack_args;
                } else {
                    args = (PSValue *)malloc(sizeof(PSValue) * (size_t)argc);
                    args_heap = 1;
                }
                if (!args) goto fail;
                for (int i = argc - 1; i >= 0; i--) {
                    args[i] = stack[--sp];
                }
                int did_throw = 0;
                PSValue throw_value = ps_value_undefined();
                PSValue result = ps_eval_call_function(vm, env, callee.as.object,
                                                       ps_value_object(vm->global),
                                                       argc, args, &did_throw, &throw_value);
                if (args_heap) free(args);
                if (did_throw) {
                    ctl->did_throw = 1;
                    ctl->throw_value = throw_value;
                    goto fail;
                }
                stack[sp++] = result;
                break;
            }
            case PS_EXPR_BC_STORE_IDENT: {
                if (sp == 0) goto fail;
                PSAstNode *id = (PSAstNode *)ins->ptr;
                PSValue value = stack[sp - 1];
                if (!ps_identifier_cached_set(env, id, value)) {
                    PSString *name = ps_identifier_string(id);
                    ps_env_set(env, name, value);
                }
                stack[sp - 1] = value;
                break;
            }
            case PS_EXPR_BC_STORE_MEMBER: {
                if (sp < 3) goto fail;
                PSValue key_val = stack[--sp];
                PSValue obj_val = stack[--sp];
                PSValue value = stack[--sp];
                PSAstNode *member = (PSAstNode *)ins->ptr;
                PSObject *obj = ps_to_object(vm, &obj_val, ctl);
                if (ctl->did_throw) goto fail;
                if (obj && obj->kind == PS_OBJ_KIND_BUFFER && ins->a) {
                    int handled = ps_buffer_write_index_value(vm, obj, key_val, value, ctl);
                    if (handled < 0) goto fail;
                    if (handled > 0) {
                        stack[sp++] = value;
                        break;
                    }
                    PSString *prop = ps_to_string(vm, key_val);
                    if (ps_check_pending_throw(vm, ctl)) goto fail;
                    handled = ps_buffer_write_index(vm, obj, prop, value, ctl);
                    if (handled < 0) goto fail;
                    if (handled > 0) {
                        stack[sp++] = value;
                        break;
                    }
                    ps_object_put(obj, prop, value);
                    stack[sp++] = value;
                    break;
                } else if (obj && obj->kind == PS_OBJ_KIND_BUFFER32 && ins->a) {
                    int handled = ps_buffer32_write_index_value(vm, obj, key_val, value, ctl);
                    if (handled < 0) goto fail;
                    if (handled > 0) {
                        stack[sp++] = value;
                        break;
                    }
                    PSString *prop = ps_to_string(vm, key_val);
                    if (ps_check_pending_throw(vm, ctl)) goto fail;
                    handled = ps_buffer32_write_index(vm, obj, prop, value, ctl);
                    if (handled < 0) goto fail;
                    if (handled > 0) {
                        stack[sp++] = value;
                        break;
                    }
                    ps_object_put(obj, prop, value);
                    stack[sp++] = value;
                    break;
                } else if (obj && obj->kind == PS_OBJ_KIND_ARRAY && ins->a) {
                    size_t index = 0;
                    int handled = ps_array_write_index_fast_value(obj, key_val, value, &index);
                    if (handled > 0) {
                        stack[sp++] = value;
                        break;
                    }
                    handled = ps_array_write_index_value(vm, obj, key_val, value, ctl);
                    if (handled < 0) goto fail;
                    if (handled > 0) {
                        stack[sp++] = value;
                        break;
                    }
                    PSString *prop = ps_to_string(vm, key_val);
                    if (ps_check_pending_throw(vm, ctl)) goto fail;
                    ps_object_put(obj, prop, value);
                    ps_array_update_length(obj, prop);
                    stack[sp++] = value;
                    break;
                } else if (obj && obj->kind == PS_OBJ_KIND_PLAIN && ins->a) {
                    size_t index = 0;
                    int idx = ps_value_to_index(vm, key_val, &index, ctl);
                    if (idx < 0) goto fail;
                    if (idx > 0 && (obj->internal_kind == PS_INTERNAL_NUMMAP || obj->internal == NULL)) {
                        int is_new = 0;
                        if (!ps_num_map_set(obj, index, value, &is_new)) goto fail;
                        if (is_new) obj->shape_id++;
                        if (env && env->arguments_obj == obj) {
                            PSString *prop = ps_array_index_string(vm, index);
                            (void)ps_env_update_arguments(env, obj, prop, value);
                        }
                        stack[sp++] = value;
                        break;
                    }
                    if (key_val.type == PS_T_STRING &&
                        (obj->internal_kind == PS_INTERNAL_NUMMAP || obj->internal == NULL)) {
                        uint32_t kindex = 0;
                        if (ps_string_to_k_index(key_val.as.string, &kindex)) {
                            int is_new = 0;
                            if (!ps_num_map_k_set(obj, kindex, value, &is_new)) goto fail;
                            if (is_new) obj->shape_id++;
                            if (env && env->arguments_obj == obj) {
                                (void)ps_env_update_arguments(env, obj, key_val.as.string, value);
                            }
                            stack[sp++] = value;
                            break;
                        }
                    }
                    PSString *prop = (key_val.type == PS_T_STRING) ? key_val.as.string
                                                                   : ps_to_string(vm, key_val);
                    if (ps_check_pending_throw(vm, ctl)) goto fail;
                    ps_object_put(obj, prop, value);
                    (void)ps_env_update_arguments(env, obj, prop, value);
                    stack[sp++] = value;
                    break;
                }
                PSString *prop = NULL;
                if (ins->a) {
                    prop = ps_member_key(vm, env, member, ctl);
                } else if (key_val.type == PS_T_STRING) {
                    prop = key_val.as.string;
                } else {
                    prop = ps_to_string(vm, key_val);
                }
                if (ctl->did_throw || ps_check_pending_throw(vm, ctl)) goto fail;
                int handled = ps_buffer_write_index(vm, obj, prop, value, ctl);
                if (handled < 0) goto fail;
                if (handled > 0) {
                    stack[sp++] = value;
                    break;
                }
                if (obj->kind == PS_OBJ_KIND_ARRAY && ps_string_is_length(prop)) {
                    int ok = ps_array_set_length(vm, obj, value, ctl);
                    if (ok < 0) goto fail;
                    stack[sp++] = value;
                    break;
                }
                ps_object_put(obj, prop, value);
                ps_array_update_length(obj, prop);
                (void)ps_env_update_arguments(env, obj, prop, value);
                stack[sp++] = value;
                break;
            }
            case PS_EXPR_BC_EVAL_AST: {
                PSAstNode *node = (PSAstNode *)ins->ptr;
                PSValue v = eval_expression_inner(vm, env, node, ctl, 0);
                if (ctl->did_throw) goto fail;
                stack[sp++] = v;
                break;
            }
            default:
                goto fail;
        }
        continue;
    }
    PSValue result = ps_value_undefined();
    if (sp > 0) result = stack[sp - 1];
    if (stack != stack_buf) free(stack);
    if (out_ok) *out_ok = 1;
    return result;
fallback:
    if (stack != stack_buf) free(stack);
    if (out_ok) *out_ok = 0;
    return ps_value_undefined();
fail:
    if (stack != stack_buf) free(stack);
    if (out_ok) *out_ok = ctl->did_throw ? 1 : 0;
    return ctl->did_throw ? ctl->throw_value : ps_value_undefined();
}

/* --------------------------------------------------------- */
/* Statement bytecode                                        */
/* --------------------------------------------------------- */

typedef struct {
    PSStmtBCInstr *code;
    size_t count;
    size_t cap;
} PSStmtBCBuilder;

typedef struct {
    size_t *breaks;
    size_t break_count;
    size_t break_cap;
    size_t *continues;
    size_t continue_count;
    size_t continue_cap;
    size_t continue_target;
} PSStmtLoop;

enum {
    STMT_BC_EVAL_EXPR = 1,
    STMT_BC_VAR_INIT,
    STMT_BC_JUMP,
    STMT_BC_JUMP_IF_FALSE,
    STMT_BC_RETURN
};

static int ps_stmt_bc_push_instr(PSStmtBCBuilder *b, PSStmtBCInstr instr) {
    if (!b) return 0;
    if (b->count == b->cap) {
        size_t new_cap = b->cap ? b->cap * 2 : 64;
        PSStmtBCInstr *next = (PSStmtBCInstr *)realloc(b->code, new_cap * sizeof(PSStmtBCInstr));
        if (!next) return 0;
        b->code = next;
        b->cap = new_cap;
    }
    b->code[b->count++] = instr;
    return 1;
}

static int ps_stmt_loop_add(size_t **list, size_t *count, size_t *cap, size_t ip) {
    if (!list || !count || !cap) return 0;
    if (*count == *cap) {
        size_t new_cap = *cap ? (*cap * 2) : 8;
        size_t *next = (size_t *)realloc(*list, sizeof(size_t) * new_cap);
        if (!next) return 0;
        *list = next;
        *cap = new_cap;
    }
    (*list)[(*count)++] = ip;
    return 1;
}

static void ps_stmt_loop_free(PSStmtLoop *loop) {
    if (!loop) return;
    free(loop->breaks);
    free(loop->continues);
    loop->breaks = NULL;
    loop->continues = NULL;
    loop->break_count = 0;
    loop->continue_count = 0;
    loop->break_cap = 0;
    loop->continue_cap = 0;
    loop->continue_target = 0;
}

static int ps_stmt_bc_emit_stmt(PSStmtBCBuilder *b,
                                PSAstNode *stmt,
                                PSStmtLoop **loops,
                                size_t *loop_depth,
                                size_t *loop_cap);

static int ps_stmt_bc_emit_list(PSStmtBCBuilder *b,
                                PSAstNode **items,
                                size_t count,
                                PSStmtLoop **loops,
                                size_t *loop_depth,
                                size_t *loop_cap) {
    if (!items) return 1;
    for (size_t i = 0; i < count; i++) {
        if (!ps_stmt_bc_emit_stmt(b, items[i], loops, loop_depth, loop_cap)) return 0;
    }
    return 1;
}

static int ps_stmt_bc_push_loop(PSStmtLoop **loops, size_t *depth, size_t *cap) {
    if (!loops || !depth || !cap) return 0;
    if (*depth == *cap) {
        size_t new_cap = *cap ? (*cap * 2) : 4;
        PSStmtLoop *next = (PSStmtLoop *)realloc(*loops, sizeof(PSStmtLoop) * new_cap);
        if (!next) return 0;
        *loops = next;
        *cap = new_cap;
    }
    PSStmtLoop *loop = &(*loops)[(*depth)++];
    memset(loop, 0, sizeof(*loop));
    return 1;
}

static PSStmtLoop *ps_stmt_bc_current_loop(PSStmtLoop *loops, size_t depth) {
    if (!loops || depth == 0) return NULL;
    return &loops[depth - 1];
}

static int ps_stmt_bc_emit_stmt(PSStmtBCBuilder *b,
                                PSAstNode *stmt,
                                PSStmtLoop **loops,
                                size_t *loop_depth,
                                size_t *loop_cap) {
    if (!stmt) return 1;
    switch (stmt->kind) {
        case AST_BLOCK:
        case AST_PROGRAM:
            return ps_stmt_bc_emit_list(b, stmt->as.list.items, stmt->as.list.count,
                                        loops, loop_depth, loop_cap);
        case AST_EXPR_STMT: {
            PSStmtBCInstr ins = {0};
            ins.op = STMT_BC_EVAL_EXPR;
            ins.ptr = stmt->as.expr_stmt.expr;
            return ps_stmt_bc_push_instr(b, ins);
        }
        case AST_VAR_DECL: {
            if (stmt->as.var_decl.init) {
                PSStmtBCInstr ins = {0};
                ins.op = STMT_BC_VAR_INIT;
                ins.ptr = stmt->as.var_decl.id;
                ins.ptr2 = stmt->as.var_decl.init;
                return ps_stmt_bc_push_instr(b, ins);
            }
            return 1;
        }
        case AST_RETURN: {
            PSStmtBCInstr ins = {0};
            ins.op = STMT_BC_RETURN;
            ins.ptr = stmt->as.ret.expr;
            return ps_stmt_bc_push_instr(b, ins);
        }
        case AST_IF: {
            PSStmtBCInstr cond = {0};
            cond.op = STMT_BC_JUMP_IF_FALSE;
            cond.ptr = stmt->as.if_stmt.cond;
            size_t cond_ip = b->count;
            if (!ps_stmt_bc_push_instr(b, cond)) return 0;
            if (!ps_stmt_bc_emit_stmt(b, stmt->as.if_stmt.then_branch,
                                      loops, loop_depth, loop_cap)) return 0;
            if (stmt->as.if_stmt.else_branch) {
                PSStmtBCInstr jmp = {0};
                jmp.op = STMT_BC_JUMP;
                size_t jmp_ip = b->count;
                if (!ps_stmt_bc_push_instr(b, jmp)) return 0;
                b->code[cond_ip].a = (int32_t)b->count;
                if (!ps_stmt_bc_emit_stmt(b, stmt->as.if_stmt.else_branch,
                                          loops, loop_depth, loop_cap)) return 0;
                b->code[jmp_ip].a = (int32_t)b->count;
            } else {
                b->code[cond_ip].a = (int32_t)b->count;
            }
            return 1;
        }
        case AST_WHILE: {
            size_t start_ip = b->count;
            PSStmtBCInstr cond = {0};
            cond.op = STMT_BC_JUMP_IF_FALSE;
            cond.ptr = stmt->as.while_stmt.cond;
            size_t cond_ip = b->count;
            if (!ps_stmt_bc_push_instr(b, cond)) return 0;
            if (!ps_stmt_bc_push_loop(loops, loop_depth, loop_cap)) return 0;
            PSStmtLoop *loop = ps_stmt_bc_current_loop(*loops, *loop_depth);
            loop->continue_target = cond_ip;
            if (!ps_stmt_bc_emit_stmt(b, stmt->as.while_stmt.body,
                                      loops, loop_depth, loop_cap)) return 0;
            PSStmtBCInstr back = {0};
            back.op = STMT_BC_JUMP;
            back.a = (int32_t)start_ip;
            if (!ps_stmt_bc_push_instr(b, back)) return 0;
            size_t end_ip = b->count;
            b->code[cond_ip].a = (int32_t)end_ip;
            for (size_t i = 0; i < loop->break_count; i++) {
                b->code[loop->breaks[i]].a = (int32_t)end_ip;
            }
            for (size_t i = 0; i < loop->continue_count; i++) {
                b->code[loop->continues[i]].a = (int32_t)loop->continue_target;
            }
            ps_stmt_loop_free(loop);
            (*loop_depth)--;
            return 1;
        }
        case AST_DO_WHILE: {
            size_t start_ip = b->count;
            if (!ps_stmt_bc_push_loop(loops, loop_depth, loop_cap)) return 0;
            if (!ps_stmt_bc_emit_stmt(b, stmt->as.do_while.body,
                                      loops, loop_depth, loop_cap)) return 0;
            PSStmtLoop *loop = ps_stmt_bc_current_loop(*loops, *loop_depth);
            size_t cond_ip = b->count;
            loop->continue_target = cond_ip;
            PSStmtBCInstr cond = {0};
            cond.op = STMT_BC_JUMP_IF_FALSE;
            cond.ptr = stmt->as.do_while.cond;
            size_t cond_jump_ip = b->count;
            if (!ps_stmt_bc_push_instr(b, cond)) return 0;
            PSStmtBCInstr back = {0};
            back.op = STMT_BC_JUMP;
            back.a = (int32_t)start_ip;
            if (!ps_stmt_bc_push_instr(b, back)) return 0;
            size_t end_ip = b->count;
            b->code[cond_jump_ip].a = (int32_t)end_ip;
            for (size_t i = 0; i < loop->break_count; i++) {
                b->code[loop->breaks[i]].a = (int32_t)end_ip;
            }
            for (size_t i = 0; i < loop->continue_count; i++) {
                b->code[loop->continues[i]].a = (int32_t)loop->continue_target;
            }
            ps_stmt_loop_free(loop);
            (*loop_depth)--;
            return 1;
        }
        case AST_FOR: {
            if (stmt->as.for_stmt.init) {
                if (!ps_stmt_bc_emit_stmt(b, stmt->as.for_stmt.init,
                                          loops, loop_depth, loop_cap)) return 0;
            }
            size_t test_ip = b->count;
            size_t test_jump_ip = (size_t)-1;
            if (stmt->as.for_stmt.test) {
                PSStmtBCInstr test = {0};
                test.op = STMT_BC_JUMP_IF_FALSE;
                test.ptr = stmt->as.for_stmt.test;
                test_jump_ip = b->count;
                if (!ps_stmt_bc_push_instr(b, test)) return 0;
            }
            if (!ps_stmt_bc_push_loop(loops, loop_depth, loop_cap)) return 0;
            if (!ps_stmt_bc_emit_stmt(b, stmt->as.for_stmt.body,
                                      loops, loop_depth, loop_cap)) return 0;
            size_t update_ip = b->count;
            if (stmt->as.for_stmt.update) {
                PSStmtBCInstr upd = {0};
                upd.op = STMT_BC_EVAL_EXPR;
                upd.ptr = stmt->as.for_stmt.update;
                if (!ps_stmt_bc_push_instr(b, upd)) return 0;
            }
            PSStmtBCInstr back = {0};
            back.op = STMT_BC_JUMP;
            back.a = (int32_t)test_ip;
            if (!ps_stmt_bc_push_instr(b, back)) return 0;
            size_t end_ip = b->count;
            if (test_jump_ip != (size_t)-1) {
                b->code[test_jump_ip].a = (int32_t)end_ip;
            }
            PSStmtLoop *loop = ps_stmt_bc_current_loop(*loops, *loop_depth);
            loop->continue_target = stmt->as.for_stmt.update ? update_ip : test_ip;
            for (size_t i = 0; i < loop->break_count; i++) {
                b->code[loop->breaks[i]].a = (int32_t)end_ip;
            }
            for (size_t i = 0; i < loop->continue_count; i++) {
                b->code[loop->continues[i]].a = (int32_t)loop->continue_target;
            }
            ps_stmt_loop_free(loop);
            (*loop_depth)--;
            return 1;
        }
        case AST_BREAK: {
            PSStmtLoop *loop = ps_stmt_bc_current_loop(*loops, *loop_depth);
            if (!loop) return 0;
            PSStmtBCInstr jmp = {0};
            jmp.op = STMT_BC_JUMP;
            size_t ip = b->count;
            if (!ps_stmt_bc_push_instr(b, jmp)) return 0;
            return ps_stmt_loop_add(&loop->breaks, &loop->break_count, &loop->break_cap, ip);
        }
        case AST_CONTINUE: {
            PSStmtLoop *loop = ps_stmt_bc_current_loop(*loops, *loop_depth);
            if (!loop) return 0;
            PSStmtBCInstr jmp = {0};
            jmp.op = STMT_BC_JUMP;
            size_t ip = b->count;
            if (!ps_stmt_bc_push_instr(b, jmp)) return 0;
            return ps_stmt_loop_add(&loop->continues, &loop->continue_count, &loop->continue_cap, ip);
        }
        case AST_FUNCTION_DECL:
            return 1;
        default:
            return 0;
    }
}

static PSStmtBC *ps_stmt_bc_compile(PSAstNode *node) {
    if (!node) return NULL;
    PSStmtBCBuilder b = {0};
    PSStmtLoop *loops = NULL;
    size_t loop_depth = 0;
    size_t loop_cap = 0;
    if (!ps_stmt_bc_emit_stmt(&b, node, &loops, &loop_depth, &loop_cap)) {
        free(b.code);
        if (loops) {
            for (size_t i = 0; i < loop_depth; i++) {
                ps_stmt_loop_free(&loops[i]);
            }
        }
        free(loops);
        return NULL;
    }
    if (loops) {
        for (size_t i = 0; i < loop_depth; i++) {
            ps_stmt_loop_free(&loops[i]);
        }
    }
    free(loops);
    PSStmtBC *bc = (PSStmtBC *)calloc(1, sizeof(PSStmtBC));
    if (!bc) {
        free(b.code);
        return NULL;
    }
    bc->code = b.code;
    bc->count = b.count;
    return bc;
}

void ps_stmt_bc_free(PSStmtBC *bc) {
    if (!bc) return;
    free(bc->code);
    free(bc);
}

static PSValue ps_stmt_bc_execute(PSVM *vm, PSEnv *env, PSFunction *func, PSStmtBC *bc, PSEvalControl *ctl) {
    (void)func;
    if (!bc || !bc->code || bc->count == 0) return ps_value_undefined();
    size_t ip = 0;
    while (ip < bc->count) {
        PSStmtBCInstr *ins = &bc->code[ip];
        switch (ins->op) {
            case STMT_BC_EVAL_EXPR: {
                if (ins->ptr) {
                    (void)eval_expression(vm, env, (PSAstNode *)ins->ptr, ctl);
                    if (ctl->did_throw) return ctl->throw_value;
                }
                ip++;
                break;
            }
            case STMT_BC_VAR_INIT: {
                PSAstNode *id = (PSAstNode *)ins->ptr;
                PSAstNode *expr = (PSAstNode *)ins->ptr2;
                if (id && expr) {
                    PSValue v = eval_expression(vm, env, expr, ctl);
                    if (ctl->did_throw) return ctl->throw_value;
                    if (!ps_identifier_cached_set(env, id, v)) {
                        PSString *name = ps_identifier_string(id);
                        if (name) {
                            ps_env_set(env, name, v);
                        }
                    }
                }
                ip++;
                break;
            }
            case STMT_BC_JUMP:
                ip = (size_t)ins->a;
                break;
            case STMT_BC_JUMP_IF_FALSE: {
                PSAstNode *cond = (PSAstNode *)ins->ptr;
                PSValue v = eval_expression(vm, env, cond, ctl);
                if (ctl->did_throw) return ctl->throw_value;
                if (!ps_to_boolean(vm, v)) {
                    ip = (size_t)ins->a;
                } else {
                    ip++;
                }
                break;
            }
            case STMT_BC_RETURN: {
                PSValue v = ps_value_undefined();
                if (ins->ptr) {
                    v = eval_expression(vm, env, (PSAstNode *)ins->ptr, ctl);
                    if (ctl->did_throw) return ctl->throw_value;
                }
                ctl->did_return = 1;
                return v;
            }
            default:
                return ps_value_undefined();
        }
    }
    return ps_value_undefined();
}

/* --------------------------------------------------------- */
/* Expressions                                               */
/* --------------------------------------------------------- */

static PSValue ps_eval_binary_values(PSVM *vm,
                                     int op,
                                     PSValue left,
                                     PSValue right,
                                     PSEvalControl *ctl) {
    switch (op) {
        case TOK_PLUS:
        {
            if (left.type == PS_T_NUMBER && right.type == PS_T_NUMBER) {
                return ps_value_number(left.as.number + right.as.number);
            }
            PSValue lprim = ps_to_primitive(vm, left, PS_HINT_NONE);
            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
            PSValue rprim = ps_to_primitive(vm, right, PS_HINT_NONE);
            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
            if (lprim.type == PS_T_STRING || rprim.type == PS_T_STRING) {
                if (lprim.type == PS_T_STRING && rprim.type == PS_T_NUMBER) {
                    PSString *out = ps_concat_string_number(vm, lprim.as.string, rprim.as.number, 0);
                    return ps_value_string(out ? out : ps_string_from_cstr(""));
                }
                if (lprim.type == PS_T_NUMBER && rprim.type == PS_T_STRING) {
                    PSString *out = ps_concat_string_number(vm, rprim.as.string, lprim.as.number, 1);
                    return ps_value_string(out ? out : ps_string_from_cstr(""));
                }
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
            if (left.type == PS_T_NUMBER && right.type == PS_T_NUMBER) {
                return ps_value_number(left.as.number - right.as.number);
            }
            double ln = ps_to_number_fast(vm, left);
            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
            double rn = ps_to_number_fast(vm, right);
            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
            return ps_value_number(ln - rn);
        }
        case TOK_STAR:
        {
            if (left.type == PS_T_NUMBER && right.type == PS_T_NUMBER) {
                return ps_value_number(left.as.number * right.as.number);
            }
            double ln = ps_to_number_fast(vm, left);
            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
            double rn = ps_to_number_fast(vm, right);
            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
            return ps_value_number(ln * rn);
        }
        case TOK_SLASH:
        {
            if (left.type == PS_T_NUMBER && right.type == PS_T_NUMBER) {
                return ps_value_number(left.as.number / right.as.number);
            }
            double ln = ps_to_number_fast(vm, left);
            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
            double rn = ps_to_number_fast(vm, right);
            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
            return ps_value_number(ln / rn);
        }
        case TOK_PERCENT:
        {
            if (left.type == PS_T_NUMBER && right.type == PS_T_NUMBER) {
                return ps_value_number(fmod(left.as.number, right.as.number));
            }
            double ln = ps_to_number_fast(vm, left);
            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
            double rn = ps_to_number_fast(vm, right);
            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
            return ps_value_number(fmod(ln, rn));
        }
        case TOK_LT:
        case TOK_LTE:
        case TOK_GT:
        case TOK_GTE: {
            if (left.type == PS_T_NUMBER && right.type == PS_T_NUMBER) {
                double ln = left.as.number;
                double rn = right.as.number;
                if (ps_value_is_nan(ln) || ps_value_is_nan(rn)) return ps_value_boolean(0);
                if (op == TOK_LT) return ps_value_boolean(ln < rn);
                if (op == TOK_LTE) return ps_value_boolean(ln <= rn);
                if (op == TOK_GT) return ps_value_boolean(ln > rn);
                return ps_value_boolean(ln >= rn);
            }
            PSValue lprim = ps_to_primitive(vm, left, PS_HINT_NUMBER);
            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
            PSValue rprim = ps_to_primitive(vm, right, PS_HINT_NUMBER);
            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
            if (lprim.type == PS_T_STRING && rprim.type == PS_T_STRING) {
                int cmp = ps_string_compare(lprim.as.string, rprim.as.string);
                if (op == TOK_LT) return ps_value_boolean(cmp < 0);
                if (op == TOK_LTE) return ps_value_boolean(cmp <= 0);
                if (op == TOK_GT) return ps_value_boolean(cmp > 0);
                return ps_value_boolean(cmp >= 0);
            }
            double ln = ps_to_number(vm, lprim);
            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
            double rn = ps_to_number(vm, rprim);
            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
            if (ps_value_is_nan(ln) || ps_value_is_nan(rn)) return ps_value_boolean(0);
            if (op == TOK_LT) return ps_value_boolean(ln < rn);
            if (op == TOK_LTE) return ps_value_boolean(ln <= rn);
            if (op == TOK_GT) return ps_value_boolean(ln > rn);
            return ps_value_boolean(ln >= rn);
        }
        case TOK_INSTANCEOF: {
            if (right.type != PS_T_OBJECT || !right.as.object ||
                right.as.object->kind != PS_OBJ_KIND_FUNCTION) {
                ctl->did_throw = 1;
                ctl->throw_value = ps_vm_make_error(vm, "TypeError", "Right-hand side of instanceof is not callable");
                return ctl->throw_value;
            }
            if (left.type != PS_T_OBJECT || !left.as.object) {
                return ps_value_boolean(0);
            }
            PSString *proto_key = ps_string_from_cstr("prototype");
            int found = 0;
            PSValue proto_val = ps_object_get(right.as.object, proto_key, &found);
            if (!found || proto_val.type != PS_T_OBJECT || !proto_val.as.object) {
                ctl->did_throw = 1;
                ctl->throw_value = ps_vm_make_error(vm, "TypeError", "Function has invalid prototype");
                return ctl->throw_value;
            }
            PSObject *target = proto_val.as.object;
            PSObject *obj = left.as.object;
            while (obj) {
                if (obj->prototype == target) {
                    return ps_value_boolean(1);
                }
                obj = obj->prototype;
            }
            return ps_value_boolean(0);
        }
        case TOK_IN: {
            if (right.type != PS_T_OBJECT || !right.as.object) {
                ctl->did_throw = 1;
                ctl->throw_value = ps_vm_make_error(vm, "TypeError", "Right-hand side of in is not an object");
                return ctl->throw_value;
            }
            PSString *key = ps_to_string(vm, left);
            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
            if (!key) return ps_value_boolean(0);
            return ps_value_boolean(ps_object_has(right.as.object, key));
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
            int32_t ln = ps_to_int32_fast(vm, left);
            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
            int32_t rn = ps_to_int32_fast(vm, right);
            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
            return ps_value_number((double)(ln & rn));
        }
        case TOK_OR:
        {
            int32_t ln = ps_to_int32_fast(vm, left);
            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
            int32_t rn = ps_to_int32_fast(vm, right);
            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
            return ps_value_number((double)(ln | rn));
        }
        case TOK_XOR:
        {
            int32_t ln = ps_to_int32_fast(vm, left);
            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
            int32_t rn = ps_to_int32_fast(vm, right);
            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
            return ps_value_number((double)(ln ^ rn));
        }
        case TOK_SHL:
        {
            int32_t ln = ps_to_int32_fast(vm, left);
            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
            uint32_t rn = ps_to_uint32_fast(vm, right);
            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
            return ps_value_number((double)(ln << (rn & 31)));
        }
        case TOK_SHR:
        {
            int32_t ln = ps_to_int32_fast(vm, left);
            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
            uint32_t rn = ps_to_uint32_fast(vm, right);
            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
            return ps_value_number((double)(ln >> (rn & 31)));
        }
        case TOK_USHR:
        {
            uint32_t ln = ps_to_uint32_fast(vm, left);
            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
            uint32_t rn = ps_to_uint32_fast(vm, right);
            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
            return ps_value_number((double)(ln >> (rn & 31)));
        }
        default:
            fprintf(stderr, "Unsupported binary operator\n");
            return ps_value_undefined();
    }
}

static PSValue eval_expression(PSVM *vm, PSEnv *env, PSAstNode *node, PSEvalControl *ctl) {
    return eval_expression_inner(vm, env, node, ctl, 1);
}

static PSValue eval_expression_inner(PSVM *vm,
                                     PSEnv *env,
                                     PSAstNode *node,
                                     PSEvalControl *ctl,
                                     int allow_bc) {
    if (vm) {
        vm->current_node = node;
#if PS_ENABLE_PERF
        vm->perf.eval_expr_count++;
        if (node && node->kind < PS_AST_KIND_COUNT) {
            vm->perf.ast_counts[node->kind]++;
        }
#endif
    }
    if (allow_bc && node) {
        if (node->expr_bc_state == 1 && node->expr_bc) {
            int ok = 0;
            PSValue v = ps_expr_bc_execute(vm, env, node, node->expr_bc, ctl, &ok);
            if (ctl->did_throw) return v;
            if (ok) return v;
        } else if (node->expr_bc_state == 0) {
            PSExprBC *bc = ps_expr_bc_compile(node);
            if (bc) {
                node->expr_bc = bc;
                node->expr_bc_state = 1;
                int ok = 0;
                PSValue v = ps_expr_bc_execute(vm, env, node, node->expr_bc, ctl, &ok);
                if (ctl->did_throw) return v;
                if (ok) return v;
                node->expr_bc_state = 2;
            } else {
                node->expr_bc_state = 2;
            }
        }
    }
    switch (node->kind) {

        case AST_LITERAL:
            return node->as.literal.value;

        case AST_FUNCTION_EXPR: {
            PSString *name = NULL;
            if (node->as.func_expr.id) {
                name = ps_identifier_string(node->as.func_expr.id);
            }
            PSObject *fn_obj = ps_function_new_script(
                node->as.func_expr.params,
                node->as.func_expr.param_defaults,
                node->as.func_expr.param_count,
                node->as.func_expr.body,
                env
            );
            if (fn_obj) {
                ps_function_setup(fn_obj, vm->function_proto, vm->object_proto, NULL);
                ps_define_script_function_props(fn_obj, name, node->as.func_expr.param_count);
                return ps_value_object(fn_obj);
            }
            return ps_value_undefined();
        }

        case AST_ARRAY_LITERAL: {
            PSObject *arr = ps_object_new(vm->array_proto ? vm->array_proto : vm->object_proto);
            if (!arr) return ps_value_undefined();
            arr->kind = PS_OBJ_KIND_ARRAY;
            (void)ps_array_init(arr);
            for (size_t i = 0; i < node->as.array_literal.count; i++) {
                if (!node->as.array_literal.items[i]) {
                    PSArray *inner = ps_array_from_object(arr);
                    if (inner) inner->dense = 0;
                    break;
                }
            }
            for (size_t i = 0; i < node->as.array_literal.count; i++) {
                if (node->as.array_literal.items[i]) {
                    PSValue v = eval_expression(vm, env, node->as.array_literal.items[i], ctl);
                    if (ctl->did_throw) return v;
                    (void)ps_array_set_index(arr, i, v);
                }
            }
            (void)ps_array_set_length_internal(arr, node->as.array_literal.count);
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
            PSString *name = ps_identifier_string(node);
            int found = 0;
            PSValue v = ps_value_undefined();
            if (!ps_identifier_cached_get(env, node, &v, &found)) {
                v = ps_env_get(env, name, &found);
            }
            if (!found) {
                ctl->did_throw = 1;
                const char *prefix = "Identifier not defined: ";
                size_t prefix_len = strlen(prefix);
                size_t name_len = name ? name->byte_len : 0;
                char *msg = NULL;
                if (name && name->utf8 && name_len > 0) {
                    msg = (char *)malloc(prefix_len + name_len + 1);
                }
                if (msg) {
                    memcpy(msg, prefix, prefix_len);
                    memcpy(msg + prefix_len, name->utf8, name_len);
                    msg[prefix_len + name_len] = '\0';
                    ctl->throw_value = ps_vm_make_error(vm, "ReferenceError", msg);
                    free(msg);
                } else {
                    ctl->throw_value = ps_vm_make_error(vm, "ReferenceError", "Identifier not defined");
                }
                return ctl->throw_value;
            }
            return v;
        }

        case AST_THIS: {
            PSString *name = ps_string_from_cstr("this");
            int found = 0;
            PSValue v = ps_env_get(env, name, &found);
            if (found) return v;
            if (vm && vm->global) {
                return ps_value_object(vm->global);
            }
            return ps_value_undefined();
        }

        case AST_ASSIGN: {
            PSValue rhs = eval_expression(vm, env, node->as.assign.value, ctl);
            if (ctl->did_throw) return rhs;
            PSValue new_value = rhs;

            if (node->as.assign.op != TOK_ASSIGN) {
                PSValue current = ps_value_undefined();
                if (node->as.assign.target->kind == AST_IDENTIFIER) {
                    int found = 0;
                    if (!ps_identifier_cached_get(env, node->as.assign.target, &current, &found)) {
                        PSString *name = ps_identifier_string(node->as.assign.target);
                        current = ps_env_get(env, name, &found);
                    }
                } else if (node->as.assign.target->kind == AST_MEMBER) {
                    PSValue obj_val = eval_expression(vm, env, node->as.assign.target->as.member.object, ctl);
                    if (ctl->did_throw) return obj_val;
                    PSObject *obj = ps_to_object(vm, &obj_val, ctl);
                    if (ctl->did_throw) return ctl->throw_value;
                    if (obj && obj->kind == PS_OBJ_KIND_BUFFER && node->as.assign.target->as.member.computed) {
                        PSValue key_val = eval_expression(vm, env, node->as.assign.target->as.member.property, ctl);
                        if (ctl->did_throw) return ctl->throw_value;
                        int handled = ps_buffer_read_index_value(vm, obj, key_val, &current, ctl);
                        if (handled < 0) return ctl->throw_value;
                        if (handled == 0) {
                            PSString *prop = ps_to_string(vm, key_val);
                            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                            handled = ps_buffer_read_index(vm, obj, prop, &current, ctl);
                            if (handled < 0) return ctl->throw_value;
                            if (handled == 0) {
                                int found;
                                current = ps_object_get(obj, prop, &found);
                            }
                        }
                    } else if (obj && obj->kind == PS_OBJ_KIND_BUFFER32 && node->as.assign.target->as.member.computed) {
                        PSValue key_val = eval_expression(vm, env, node->as.assign.target->as.member.property, ctl);
                        if (ctl->did_throw) return ctl->throw_value;
                        int handled = ps_buffer32_read_index_value(vm, obj, key_val, &current, ctl);
                        if (handled < 0) return ctl->throw_value;
                        if (handled == 0) {
                            PSString *prop = ps_to_string(vm, key_val);
                            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                            handled = ps_buffer32_read_index(vm, obj, prop, &current, ctl);
                            if (handled < 0) return ctl->throw_value;
                            if (handled == 0) {
                                int found;
                                current = ps_object_get(obj, prop, &found);
                            }
                        }
                    } else if (obj && obj->kind == PS_OBJ_KIND_ARRAY && node->as.assign.target->as.member.computed) {
                        PSValue key_val = eval_expression(vm, env, node->as.assign.target->as.member.property, ctl);
                        if (ctl->did_throw) return ctl->throw_value;
                        size_t index = 0;
                        int handled = ps_array_read_index_fast_value(obj, key_val, &current, &index);
                        if (handled < 0) {
                            PSString *prop = ps_array_index_string(vm, index);
                            if (!ps_member_cached_get(obj, node->as.assign.target, prop, &current)) {
                                int found;
                                current = ps_object_get(obj, prop, &found);
                            }
                        } else if (handled == 0) {
                            handled = ps_array_read_index_value(vm, obj, key_val, &current, ctl);
                            if (handled < 0) return ctl->throw_value;
                            if (handled == 0) {
                                PSString *prop = ps_to_string(vm, key_val);
                                if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                                if (!ps_member_cached_get(obj, node->as.assign.target, prop, &current)) {
                                    int found;
                                    current = ps_object_get(obj, prop, &found);
                                }
                            }
                        }
                    } else if (obj && obj->kind == PS_OBJ_KIND_PLAIN && node->as.assign.target->as.member.computed) {
                        PSValue key_val = eval_expression(vm, env, node->as.assign.target->as.member.property, ctl);
                        if (ctl->did_throw) return ctl->throw_value;
                        size_t index = 0;
                        int idx = ps_value_to_index(vm, key_val, &index, ctl);
                        if (idx < 0) return ctl->throw_value;
                        if (idx > 0 && obj->internal_kind == PS_INTERNAL_NUMMAP) {
                            PSValue out = ps_value_undefined();
                            if (ps_num_map_get(obj, index, &out)) {
                                current = out;
                            } else {
                                current = ps_value_undefined();
                            }
                        } else {
                            int got_k = 0;
                            if (key_val.type == PS_T_STRING && obj->internal_kind == PS_INTERNAL_NUMMAP) {
                                uint32_t kindex = 0;
                                if (ps_string_to_k_index(key_val.as.string, &kindex)) {
                                    PSValue out = ps_value_undefined();
                                    if (ps_num_map_k_get(obj, kindex, &out)) {
                                        current = out;
                                        got_k = 1;
                                    }
                                }
                            }
                            if (!got_k) {
                                PSString *prop = (key_val.type == PS_T_STRING) ? key_val.as.string
                                                                             : ps_to_string(vm, key_val);
                                if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                                int found;
                                current = ps_object_get(obj, prop, &found);
                            }
                        }
                    } else {
                        PSString *prop = ps_member_key(vm, env, node->as.assign.target, ctl);
                        if (ctl->did_throw) return ctl->throw_value;
                        int handled = ps_buffer_read_index(vm, obj, prop, &current, ctl);
                        if (handled < 0) return ctl->throw_value;
                        if (handled == 0) {
                            if (!ps_member_cached_get(obj, node->as.assign.target, prop, &current)) {
                                int found;
                                current = ps_object_get(obj, prop, &found);
                            }
                        }
                    }
                }

                switch (node->as.assign.op) {
                    case TOK_PLUS_ASSIGN:
                    {
                        if (current.type == PS_T_NUMBER && rhs.type == PS_T_NUMBER) {
                            new_value = ps_value_number(current.as.number + rhs.as.number);
                            break;
                        }
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
                        if (current.type == PS_T_NUMBER && rhs.type == PS_T_NUMBER) {
                            new_value = ps_value_number(current.as.number - rhs.as.number);
                            break;
                        }
                        double ln = ps_to_number_fast(vm, current);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        double rn = ps_to_number_fast(vm, rhs);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        new_value = ps_value_number(ln - rn);
                        break;
                    }
                    case TOK_STAR_ASSIGN:
                    {
                        if (current.type == PS_T_NUMBER && rhs.type == PS_T_NUMBER) {
                            new_value = ps_value_number(current.as.number * rhs.as.number);
                            break;
                        }
                        double ln = ps_to_number_fast(vm, current);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        double rn = ps_to_number_fast(vm, rhs);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        new_value = ps_value_number(ln * rn);
                        break;
                    }
                    case TOK_SLASH_ASSIGN:
                    {
                        if (current.type == PS_T_NUMBER && rhs.type == PS_T_NUMBER) {
                            new_value = ps_value_number(current.as.number / rhs.as.number);
                            break;
                        }
                        double ln = ps_to_number_fast(vm, current);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        double rn = ps_to_number_fast(vm, rhs);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        new_value = ps_value_number(ln / rn);
                        break;
                    }
                    case TOK_PERCENT_ASSIGN:
                    {
                        if (current.type == PS_T_NUMBER && rhs.type == PS_T_NUMBER) {
                            new_value = ps_value_number(fmod(current.as.number, rhs.as.number));
                            break;
                        }
                        double ln = ps_to_number_fast(vm, current);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        double rn = ps_to_number_fast(vm, rhs);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        new_value = ps_value_number(fmod(ln, rn));
                        break;
                    }
                    case TOK_SHL_ASSIGN:
                    {
                        int32_t ln = ps_to_int32_fast(vm, current);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        uint32_t rn = ps_to_uint32_fast(vm, rhs);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        new_value = ps_value_number((double)(ln << (rn & 31)));
                        break;
                    }
                    case TOK_SHR_ASSIGN:
                    {
                        int32_t ln = ps_to_int32_fast(vm, current);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        uint32_t rn = ps_to_uint32_fast(vm, rhs);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        new_value = ps_value_number((double)(ln >> (rn & 31)));
                        break;
                    }
                    case TOK_USHR_ASSIGN:
                    {
                        uint32_t ln = ps_to_uint32_fast(vm, current);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        uint32_t rn = ps_to_uint32_fast(vm, rhs);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        new_value = ps_value_number((double)(ln >> (rn & 31)));
                        break;
                    }
                    case TOK_AND_ASSIGN:
                    {
                        int32_t ln = ps_to_int32_fast(vm, current);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        int32_t rn = ps_to_int32_fast(vm, rhs);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        new_value = ps_value_number((double)(ln & rn));
                        break;
                    }
                    case TOK_OR_ASSIGN:
                    {
                        int32_t ln = ps_to_int32_fast(vm, current);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        int32_t rn = ps_to_int32_fast(vm, rhs);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        new_value = ps_value_number((double)(ln | rn));
                        break;
                    }
                    case TOK_XOR_ASSIGN:
                    {
                        int32_t ln = ps_to_int32_fast(vm, current);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        int32_t rn = ps_to_int32_fast(vm, rhs);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        new_value = ps_value_number((double)(ln ^ rn));
                        break;
                    }
                    default:
                        break;
                }
            }

            if (node->as.assign.target->kind == AST_IDENTIFIER) {
                PSString *name = ps_identifier_string(node->as.assign.target);
                if (!ps_identifier_cached_set(env, node->as.assign.target, new_value)) {
                    ps_env_set(env, name, new_value);
                }
                return new_value;
            }
            if (node->as.assign.target->kind == AST_MEMBER) {
                PSValue obj_val = eval_expression(vm, env, node->as.assign.target->as.member.object, ctl);
                if (ctl->did_throw) return obj_val;
                PSObject *obj = ps_to_object(vm, &obj_val, ctl);
                if (ctl->did_throw) return ctl->throw_value;
                if (obj && obj->kind == PS_OBJ_KIND_BUFFER && node->as.assign.target->as.member.computed) {
                    PSValue key_val = eval_expression(vm, env, node->as.assign.target->as.member.property, ctl);
                    if (ctl->did_throw) return ctl->throw_value;
                    int handled = ps_buffer_write_index_value(vm, obj, key_val, new_value, ctl);
                    if (handled < 0) return ctl->throw_value;
                    if (handled > 0) return new_value;
                    PSString *prop = ps_to_string(vm, key_val);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    handled = ps_buffer_write_index(vm, obj, prop, new_value, ctl);
                    if (handled < 0) return ctl->throw_value;
                    if (handled > 0) return new_value;
                    ps_object_put(obj, prop, new_value);
                    ps_array_update_length(obj, prop);
                    (void)ps_env_update_arguments(env, obj, prop, new_value);
                    return new_value;
                } else if (obj && obj->kind == PS_OBJ_KIND_BUFFER32 && node->as.assign.target->as.member.computed) {
                    PSValue key_val = eval_expression(vm, env, node->as.assign.target->as.member.property, ctl);
                    if (ctl->did_throw) return ctl->throw_value;
                    int handled = ps_buffer32_write_index_value(vm, obj, key_val, new_value, ctl);
                    if (handled < 0) return ctl->throw_value;
                    if (handled > 0) return new_value;
                    PSString *prop = ps_to_string(vm, key_val);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    handled = ps_buffer32_write_index(vm, obj, prop, new_value, ctl);
                    if (handled < 0) return ctl->throw_value;
                    if (handled > 0) return new_value;
                    ps_object_put(obj, prop, new_value);
                    (void)ps_env_update_arguments(env, obj, prop, new_value);
                    return new_value;
                } else if (obj && obj->kind == PS_OBJ_KIND_ARRAY && node->as.assign.target->as.member.computed) {
                    PSValue key_val = eval_expression(vm, env, node->as.assign.target->as.member.property, ctl);
                    if (ctl->did_throw) return ctl->throw_value;
                    size_t index = 0;
                    int handled = ps_array_write_index_fast_value(obj, key_val, new_value, &index);
                    if (handled > 0) return new_value;
                    handled = ps_array_write_index_value(vm, obj, key_val, new_value, ctl);
                    if (handled < 0) return ctl->throw_value;
                    if (handled > 0) return new_value;
                    PSString *prop = ps_to_string(vm, key_val);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    ps_object_put(obj, prop, new_value);
                    ps_array_update_length(obj, prop);
                    (void)ps_env_update_arguments(env, obj, prop, new_value);
                    return new_value;
                } else if (obj && obj->kind == PS_OBJ_KIND_PLAIN && node->as.assign.target->as.member.computed) {
                    PSValue key_val = eval_expression(vm, env, node->as.assign.target->as.member.property, ctl);
                    if (ctl->did_throw) return ctl->throw_value;
                    size_t index = 0;
                    int idx = ps_value_to_index(vm, key_val, &index, ctl);
                    if (idx < 0) return ctl->throw_value;
                    if (idx > 0 && (obj->internal_kind == PS_INTERNAL_NUMMAP || obj->internal == NULL)) {
                        if (obj->props) {
                            PSString *prop = ps_array_index_string(vm, index);
                            PSProperty *p = ps_object_get_own_prop(obj, prop);
                            if (p) {
                                p->value = new_value;
                                obj->cache_name = p->name;
                                obj->cache_prop = p;
                                return new_value;
                            }
                        }
                        int is_new = 0;
                        if (!ps_num_map_set(obj, index, new_value, &is_new)) return new_value;
                        if (is_new) obj->shape_id++;
                        return new_value;
                    }
                    if (key_val.type == PS_T_STRING &&
                        (obj->internal_kind == PS_INTERNAL_NUMMAP || obj->internal == NULL)) {
                        uint32_t kindex = 0;
                        if (ps_string_to_k_index(key_val.as.string, &kindex)) {
                            if (obj->props) {
                                PSProperty *p = ps_object_get_own_prop(obj, key_val.as.string);
                                if (p) {
                                    p->value = new_value;
                                    obj->cache_name = p->name;
                                    obj->cache_prop = p;
                                    return new_value;
                                }
                            }
                            int is_new = 0;
                            if (!ps_num_map_k_set(obj, kindex, new_value, &is_new)) return new_value;
                            if (is_new) obj->shape_id++;
                            return new_value;
                        }
                    }
                    PSString *prop = (key_val.type == PS_T_STRING) ? key_val.as.string : ps_to_string(vm, key_val);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    ps_object_put(obj, prop, new_value);
                    return new_value;
                }
                PSString *prop = ps_member_key(vm, env, node->as.assign.target, ctl);
                if (ctl->did_throw) return ctl->throw_value;
                int handled = ps_buffer_write_index(vm, obj, prop, new_value, ctl);
                if (handled < 0) return ctl->throw_value;
                if (handled > 0) return new_value;
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
            ctl->did_throw = 1;
            {
                char *target = ps_format_call_target_name(node->as.assign.target);
                if (target) {
                    const char *prefix = "Invalid assignment target: ";
                    size_t prefix_len = strlen(prefix);
                    size_t target_len = strlen(target);
                    char *msg = (char *)malloc(prefix_len + target_len + 1);
                    if (msg) {
                        memcpy(msg, prefix, prefix_len);
                        memcpy(msg + prefix_len, target, target_len);
                        msg[prefix_len + target_len] = '\0';
                        ctl->throw_value = ps_vm_make_error(vm, "SyntaxError", msg);
                        free(msg);
                    } else {
                        ctl->throw_value = ps_vm_make_error(vm, "SyntaxError", "Invalid assignment target");
                    }
                    free(target);
                } else {
                    ctl->throw_value = ps_vm_make_error(vm, "SyntaxError", "Invalid assignment target");
                }
            }
            return ctl->throw_value;
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
            return ps_eval_binary_values(vm, node->as.binary.op, left, right, ctl);
        }

        case AST_UNARY: {
            PSValue v = ps_value_undefined();
            if (node->as.unary.op == TOK_TYPEOF &&
                node->as.unary.expr->kind == AST_IDENTIFIER) {
                PSAstNode *id = node->as.unary.expr;
                PSString *name_id = ps_identifier_string(id);
                int found = 0;
                if (!ps_identifier_cached_get(env, id, &v, &found)) {
                    v = ps_env_get(env, name_id, &found);
                }
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
                    return ps_value_number((double)(~ps_to_int32_fast(vm, v)));
                case TOK_PLUS:
                {
                    double num = ps_to_number_fast(vm, v);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    return ps_value_number(num);
                }
                case TOK_MINUS:
                {
                    double num = ps_to_number_fast(vm, v);
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
                        if (obj && obj->kind == PS_OBJ_KIND_ARRAY && mem->as.member.computed) {
                            PSValue key_val = eval_expression(vm, env, mem->as.member.property, ctl);
                            if (ctl->did_throw) return ctl->throw_value;
                            size_t index = 0;
                            int idx = ps_value_to_array_index(vm, key_val, &index, ctl);
                            if (idx < 0) return ctl->throw_value;
                            if (idx > 0) {
                                (void)ps_array_delete_index(obj, index);
                                return ps_value_boolean(1);
                            }
                        }
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
                PSString *name = ps_identifier_string(target);
                int found = 0;
                if (!ps_identifier_cached_get(env, target, &current, &found)) {
                    current = ps_env_get(env, name, &found);
                }
                double num = ps_to_number(vm, current);
                if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                double new_num = (node->as.update.op == TOK_PLUS_PLUS) ? num + 1 : num - 1;
                PSValue new_val = ps_value_number(new_num);
                if (!ps_identifier_cached_set(env, target, new_val)) {
                    ps_env_set(env, name, new_val);
                }
                return node->as.update.is_prefix ? new_val : current;
            }
            if (target->kind == AST_MEMBER) {
                PSValue obj_val = eval_expression(vm, env, target->as.member.object, ctl);
                if (ctl->did_throw) return obj_val;
                PSObject *obj = ps_to_object(vm, &obj_val, ctl);
                if (ctl->did_throw) return ctl->throw_value;
                if (obj && obj->kind == PS_OBJ_KIND_BUFFER && target->as.member.computed) {
                    PSValue key_val = eval_expression(vm, env, target->as.member.property, ctl);
                    if (ctl->did_throw) return ctl->throw_value;
                    int handled = ps_buffer_read_index_value(vm, obj, key_val, &current, ctl);
                    if (handled < 0) return ctl->throw_value;
                    if (handled > 0) {
                        double num = ps_to_number(vm, current);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        double new_num = (node->as.update.op == TOK_PLUS_PLUS) ? num + 1 : num - 1;
                        PSValue new_val = ps_value_number(new_num);
                        int wrote = ps_buffer_write_index_value(vm, obj, key_val, new_val, ctl);
                        if (wrote < 0) return ctl->throw_value;
                        return node->as.update.is_prefix ? new_val : current;
                    }
                    PSString *prop = ps_to_string(vm, key_val);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    handled = ps_buffer_read_index(vm, obj, prop, &current, ctl);
                    if (handled < 0) return ctl->throw_value;
                    if (handled > 0) {
                        double num = ps_to_number(vm, current);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        double new_num = (node->as.update.op == TOK_PLUS_PLUS) ? num + 1 : num - 1;
                        PSValue new_val = ps_value_number(new_num);
                        int wrote = ps_buffer_write_index(vm, obj, prop, new_val, ctl);
                        if (wrote < 0) return ctl->throw_value;
                        return node->as.update.is_prefix ? new_val : current;
                    }
                    if (!ps_member_cached_get(obj, target, prop, &current)) {
                        int found;
                        current = ps_object_get(obj, prop, &found);
                    }
                    double num = ps_to_number(vm, current);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    double new_num = (node->as.update.op == TOK_PLUS_PLUS) ? num + 1 : num - 1;
                    PSValue new_val = ps_value_number(new_num);
                    ps_object_put(obj, prop, new_val);
                    ps_array_update_length(obj, prop);
                    (void)ps_env_update_arguments(env, obj, prop, new_val);
                    return node->as.update.is_prefix ? new_val : current;
                } else if (obj && obj->kind == PS_OBJ_KIND_BUFFER32 && target->as.member.computed) {
                    PSValue key_val = eval_expression(vm, env, target->as.member.property, ctl);
                    if (ctl->did_throw) return ctl->throw_value;
                    int handled = ps_buffer32_read_index_value(vm, obj, key_val, &current, ctl);
                    if (handled < 0) return ctl->throw_value;
                    if (handled > 0) {
                        double num = ps_to_number(vm, current);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        double new_num = (node->as.update.op == TOK_PLUS_PLUS) ? num + 1 : num - 1;
                        PSValue new_val = ps_value_number(new_num);
                        int wrote = ps_buffer32_write_index_value(vm, obj, key_val, new_val, ctl);
                        if (wrote < 0) return ctl->throw_value;
                        return node->as.update.is_prefix ? new_val : current;
                    }
                    PSString *prop = ps_to_string(vm, key_val);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    handled = ps_buffer32_read_index(vm, obj, prop, &current, ctl);
                    if (handled < 0) return ctl->throw_value;
                    if (handled > 0) {
                        double num = ps_to_number(vm, current);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        double new_num = (node->as.update.op == TOK_PLUS_PLUS) ? num + 1 : num - 1;
                        PSValue new_val = ps_value_number(new_num);
                        int wrote = ps_buffer32_write_index(vm, obj, prop, new_val, ctl);
                        if (wrote < 0) return ctl->throw_value;
                        return node->as.update.is_prefix ? new_val : current;
                    }
                    if (!ps_member_cached_get(obj, target, prop, &current)) {
                        int found;
                        current = ps_object_get(obj, prop, &found);
                    }
                    double num = ps_to_number(vm, current);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    double new_num = (node->as.update.op == TOK_PLUS_PLUS) ? num + 1 : num - 1;
                    PSValue new_val = ps_value_number(new_num);
                    ps_object_put(obj, prop, new_val);
                    (void)ps_env_update_arguments(env, obj, prop, new_val);
                    return node->as.update.is_prefix ? new_val : current;
                } else if (obj && obj->kind == PS_OBJ_KIND_ARRAY && target->as.member.computed) {
                    PSValue key_val = eval_expression(vm, env, target->as.member.property, ctl);
                    if (ctl->did_throw) return ctl->throw_value;
                    PSString *prop = NULL;
                    size_t index = 0;
                    int handled = ps_array_read_index_fast_value(obj, key_val, &current, &index);
                    if (handled > 0) {
                        double num = ps_to_number(vm, current);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        double new_num = (node->as.update.op == TOK_PLUS_PLUS) ? num + 1 : num - 1;
                        PSValue new_val = ps_value_number(new_num);
                        int wrote = ps_array_write_index_fast_value(obj, key_val, new_val, NULL);
                        if (wrote == 0) {
                            wrote = ps_array_write_index_value(vm, obj, key_val, new_val, ctl);
                        }
                        if (wrote < 0) return ctl->throw_value;
                        return node->as.update.is_prefix ? new_val : current;
                    }
                    if (handled < 0) {
                        prop = ps_array_index_string(vm, index);
                        if (!ps_member_cached_get(obj, target, prop, &current)) {
                            int found;
                            current = ps_object_get(obj, prop, &found);
                        }
                    } else {
                        handled = ps_array_read_index_value(vm, obj, key_val, &current, ctl);
                        if (handled < 0) return ctl->throw_value;
                        if (handled > 0) {
                            double num = ps_to_number(vm, current);
                            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                            double new_num = (node->as.update.op == TOK_PLUS_PLUS) ? num + 1 : num - 1;
                            PSValue new_val = ps_value_number(new_num);
                            int wrote = ps_array_write_index_value(vm, obj, key_val, new_val, ctl);
                            if (wrote < 0) return ctl->throw_value;
                            return node->as.update.is_prefix ? new_val : current;
                        }
                        if (handled == 0) {
                            prop = ps_to_string(vm, key_val);
                            if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                            if (!ps_member_cached_get(obj, target, prop, &current)) {
                                int found;
                                current = ps_object_get(obj, prop, &found);
                            }
                        }
                    }
                    if (!prop) {
                        return ps_value_undefined();
                    }
                    double num = ps_to_number(vm, current);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    double new_num = (node->as.update.op == TOK_PLUS_PLUS) ? num + 1 : num - 1;
                    PSValue new_val = ps_value_number(new_num);
                    ps_object_put(obj, prop, new_val);
                    ps_array_update_length(obj, prop);
                    (void)ps_env_update_arguments(env, obj, prop, new_val);
                    return node->as.update.is_prefix ? new_val : current;
                } else if (obj && obj->kind == PS_OBJ_KIND_PLAIN && target->as.member.computed) {
                    PSValue key_val = eval_expression(vm, env, target->as.member.property, ctl);
                    if (ctl->did_throw) return ctl->throw_value;
                    size_t index = 0;
                    int idx = ps_value_to_index(vm, key_val, &index, ctl);
                    if (idx < 0) return ctl->throw_value;
                    if (idx > 0 && (obj->internal_kind == PS_INTERNAL_NUMMAP || obj->internal == NULL)) {
                        PSProperty *p = NULL;
                        if (obj->props) {
                            PSString *prop = ps_array_index_string(vm, index);
                            p = ps_object_get_own_prop(obj, prop);
                        }
                        if (p) {
                            current = p->value;
                        } else {
                            PSValue out = ps_value_undefined();
                            if (obj->internal_kind == PS_INTERNAL_NUMMAP) {
                                if (ps_num_map_get(obj, index, &out)) current = out;
                                else current = ps_value_undefined();
                            } else {
                                current = ps_value_undefined();
                            }
                        }
                        double num = ps_to_number(vm, current);
                        if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                        double new_num = (node->as.update.op == TOK_PLUS_PLUS) ? num + 1 : num - 1;
                        PSValue new_val = ps_value_number(new_num);
                        if (p) {
                            p->value = new_val;
                            obj->cache_name = p->name;
                            obj->cache_prop = p;
                        } else {
                            int is_new = 0;
                            if (ps_num_map_set(obj, index, new_val, &is_new) && is_new) {
                                obj->shape_id++;
                            }
                        }
                        return node->as.update.is_prefix ? new_val : current;
                    }
                    PSString *prop = ps_to_string(vm, key_val);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    if (!ps_member_cached_get(obj, target, prop, &current)) {
                        int found;
                        current = ps_object_get(obj, prop, &found);
                    }
                    double num = ps_to_number(vm, current);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    double new_num = (node->as.update.op == TOK_PLUS_PLUS) ? num + 1 : num - 1;
                    PSValue new_val = ps_value_number(new_num);
                    ps_object_put(obj, prop, new_val);
                    (void)ps_env_update_arguments(env, obj, prop, new_val);
                    return node->as.update.is_prefix ? new_val : current;
                }
                PSString *prop = ps_member_key(vm, env, target, ctl);
                if (ctl->did_throw) return ctl->throw_value;
                int handled = ps_buffer_read_index(vm, obj, prop, &current, ctl);
                if (handled < 0) return ctl->throw_value;
                if (handled > 0) {
                    double num = ps_to_number(vm, current);
                    if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                    double new_num = (node->as.update.op == TOK_PLUS_PLUS) ? num + 1 : num - 1;
                    PSValue new_val = ps_value_number(new_num);
                    int wrote = ps_buffer_write_index(vm, obj, prop, new_val, ctl);
                    if (wrote < 0) return ctl->throw_value;
                    return node->as.update.is_prefix ? new_val : current;
                }
                if (!ps_member_cached_get(obj, target, prop, &current)) {
                    int found;
                    current = ps_object_get(obj, prop, &found);
                }
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
            if (obj && obj->kind == PS_OBJ_KIND_BUFFER && node->as.member.computed) {
                PSValue key_val = eval_expression(vm, env, node->as.member.property, ctl);
                if (ctl->did_throw) return ctl->throw_value;
                PSValue out = ps_value_undefined();
                int handled = ps_buffer_read_index_value(vm, obj, key_val, &out, ctl);
                if (handled < 0) return ctl->throw_value;
                if (handled > 0) return out;
                PSString *prop = ps_to_string(vm, key_val);
                if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                handled = ps_buffer_read_index(vm, obj, prop, &out, ctl);
                if (handled < 0) return ctl->throw_value;
                if (handled > 0) return out;
                int found;
                return ps_object_get(obj, prop, &found);
            } else if (obj && obj->kind == PS_OBJ_KIND_BUFFER32 && node->as.member.computed) {
                PSValue key_val = eval_expression(vm, env, node->as.member.property, ctl);
                if (ctl->did_throw) return ctl->throw_value;
                PSValue out = ps_value_undefined();
                int handled = ps_buffer32_read_index_value(vm, obj, key_val, &out, ctl);
                if (handled < 0) return ctl->throw_value;
                if (handled > 0) return out;
                PSString *prop = ps_to_string(vm, key_val);
                if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                handled = ps_buffer32_read_index(vm, obj, prop, &out, ctl);
                if (handled < 0) return ctl->throw_value;
                if (handled > 0) return out;
                int found;
                return ps_object_get(obj, prop, &found);
            } else if (obj && obj->kind == PS_OBJ_KIND_ARRAY && node->as.member.computed) {
                PSValue key_val = eval_expression(vm, env, node->as.member.property, ctl);
                if (ctl->did_throw) return ctl->throw_value;
                PSValue out = ps_value_undefined();
                size_t index = 0;
                int handled = ps_array_read_index_fast_value(obj, key_val, &out, &index);
                if (handled > 0) return out;
                if (handled < 0) {
                    PSString *prop = ps_array_index_string(vm, index);
                    int found;
                    return ps_object_get(obj, prop, &found);
                }
                handled = ps_array_read_index_value(vm, obj, key_val, &out, ctl);
                if (handled < 0) return ctl->throw_value;
                if (handled > 0) return out;
                PSString *prop = ps_to_string(vm, key_val);
                if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                int found;
                return ps_object_get(obj, prop, &found);
            } else if (obj && obj->kind == PS_OBJ_KIND_PLAIN && node->as.member.computed) {
                PSValue key_val = eval_expression(vm, env, node->as.member.property, ctl);
                if (ctl->did_throw) return ctl->throw_value;
                size_t index = 0;
                int idx = ps_value_to_index(vm, key_val, &index, ctl);
                if (idx < 0) return ctl->throw_value;
                if (idx > 0 && obj->internal_kind == PS_INTERNAL_NUMMAP) {
                    PSValue out = ps_value_undefined();
                    if (ps_num_map_get(obj, index, &out)) return out;
                    return ps_value_undefined();
                }
                if (key_val.type == PS_T_STRING && obj->internal_kind == PS_INTERNAL_NUMMAP) {
                    uint32_t kindex = 0;
                    if (ps_string_to_k_index(key_val.as.string, &kindex)) {
                        PSValue out = ps_value_undefined();
                        if (ps_num_map_k_get(obj, kindex, &out)) return out;
                    }
                }
                PSString *prop = (key_val.type == PS_T_STRING) ? key_val.as.string : ps_to_string(vm, key_val);
                if (ps_check_pending_throw(vm, ctl)) return ctl->throw_value;
                int found;
                return ps_object_get(obj, prop, &found);
            }
            PSString tmp_prop;
            char tmp_buf[96];
            PSString *prop = ps_member_key_read(vm, env, node, ctl, &tmp_prop, tmp_buf, sizeof(tmp_buf));
            if (ctl->did_throw) return ctl->throw_value;
            PSValue out = ps_value_undefined();
            int handled = ps_buffer_read_index(vm, obj, prop, &out, ctl);
            if (handled < 0) return ctl->throw_value;
            if (handled > 0) return out;
            if (ps_member_cached_get(obj, node, prop, &out)) return out;
            int found;
            return ps_object_get(obj, prop, &found);
        }

        case AST_CALL: {
#if PS_ENABLE_PERF
            if (vm && node->as.call.callee) {
                if (node->as.call.callee->kind == AST_IDENTIFIER) {
                    vm->perf.call_ident_count++;
                } else if (node->as.call.callee->kind == AST_MEMBER) {
                    vm->perf.call_member_count++;
                } else {
                    vm->perf.call_other_count++;
                }
            }
#endif
            if (vm) {
                PSValue fast_out = ps_value_undefined();
                int fast_handled = ps_eval_math_intrinsic(vm, env, node, ctl, &fast_out);
                if (fast_handled < 0) return ctl->throw_value;
                if (fast_handled > 0) return fast_out;
            }
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
                    PSValue stack_args[8];
                    int args_heap = 0;
                    if (!ps_eval_args(vm, env, node->as.call.args, node->as.call.argc,
                                      stack_args, 8, &args, &args_heap, ctl)) {
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
                    if (args_heap) {
                        free(args);
                    }
                    return result;
                }
            }

            PSAstNode *callee_node = node->as.call.callee;
            PSValue this_val = ps_value_undefined();
            PSValue callee = ps_value_undefined();

            if (callee_node->kind == AST_IDENTIFIER) {
                if (node->as.call.cache_kind == PS_CALL_CACHE_IDENT_FAST &&
                    node->as.call.cache_fast_env == env &&
                    env->fast_values &&
                    node->as.call.cache_fast_index < env->fast_count) {
                    callee = env->fast_values[node->as.call.cache_fast_index];
                    this_val = ps_value_object(vm->global);
                    goto call_have_callee;
                }
                if (node->as.call.cache_kind == PS_CALL_CACHE_IDENT_PROP &&
                    node->as.call.cache_env == env &&
                    node->as.call.cache_record == env->record &&
                    env->record &&
                    node->as.call.cache_shape == env->record->shape_id &&
                    node->as.call.cache_prop) {
                    callee = node->as.call.cache_prop->value;
                    this_val = ps_value_object(vm->global);
                    goto call_have_callee;
                }
                PSAstNode *id = callee_node;
                PSString *name = ps_identifier_string(id);
                int found = 0;
                if (!ps_identifier_cached_get(env, id, &callee, &found)) {
                    callee = ps_env_get(env, name, &found);
                }
                if (!found) {
                    ctl->did_throw = 1;
                    const char *prefix = "Identifier not defined: ";
                    size_t prefix_len = strlen(prefix);
                    size_t name_len = name ? name->byte_len : 0;
                    char *msg = NULL;
                    if (name && name->utf8 && name_len > 0) {
                        msg = (char *)malloc(prefix_len + name_len + 1);
                    }
                    if (msg) {
                        memcpy(msg, prefix, prefix_len);
                        memcpy(msg + prefix_len, name->utf8, name_len);
                        msg[prefix_len + name_len] = '\0';
                        ctl->throw_value = ps_vm_make_error(vm, "ReferenceError", msg);
                        free(msg);
                    } else {
                        ctl->throw_value = ps_vm_make_error(vm, "ReferenceError", "Identifier not defined");
                    }
                    return ctl->throw_value;
                }
                this_val = ps_value_object(vm->global);
                if (id->as.identifier.cache_fast_env == env) {
                    node->as.call.cache_kind = PS_CALL_CACHE_IDENT_FAST;
                    node->as.call.cache_fast_env = env;
                    node->as.call.cache_fast_index = id->as.identifier.cache_fast_index;
                } else if (id->as.identifier.cache_env == env &&
                           id->as.identifier.cache_record == env->record &&
                           id->as.identifier.cache_prop &&
                           env->record) {
                    node->as.call.cache_kind = PS_CALL_CACHE_IDENT_PROP;
                    node->as.call.cache_env = env;
                    node->as.call.cache_record = env->record;
                    node->as.call.cache_prop = id->as.identifier.cache_prop;
                    node->as.call.cache_shape = env->record->shape_id;
                } else {
                    node->as.call.cache_kind = PS_CALL_CACHE_NONE;
                }
            } else if (callee_node->kind == AST_MEMBER) {
                PSAstNode *member = callee_node;
                PSValue obj_val = eval_expression(vm, env, member->as.member.object, ctl);
                if (ctl->did_throw) return obj_val;
                PSObject *obj = ps_to_object(vm, &obj_val, ctl);
                if (ctl->did_throw) return ctl->throw_value;
                if (!member->as.member.computed &&
                    node->as.call.cache_kind == PS_CALL_CACHE_MEMBER &&
                    node->as.call.cache_obj == obj &&
                    node->as.call.cache_member_shape == obj->shape_id &&
                    node->as.call.cache_member_prop) {
                    callee = node->as.call.cache_member_prop->value;
                    this_val = ps_value_object(obj);
                    goto call_have_callee;
                }
                PSString tmp_prop;
                char tmp_buf[96];
                PSString *prop = ps_member_key_read(vm, env, member, ctl, &tmp_prop, tmp_buf, sizeof(tmp_buf));
                if (ctl->did_throw) return ctl->throw_value;
                if (member->as.member.computed || !ps_member_cached_get(obj, member, prop, &callee)) {
                    int found;
                    callee = ps_object_get(obj, prop, &found);
                }
                this_val = ps_value_object(obj);
                if (!member->as.member.computed &&
                    member->as.member.cache_obj == obj &&
                    member->as.member.cache_prop) {
                    node->as.call.cache_kind = PS_CALL_CACHE_MEMBER;
                    node->as.call.cache_obj = obj;
                    node->as.call.cache_member_prop = member->as.member.cache_prop;
                    node->as.call.cache_member_shape = obj->shape_id;
                }
            } else {
                callee = eval_expression(vm, env, callee_node, ctl);
                if (ctl->did_throw) return callee;
                this_val = ps_value_object(vm->global);
            }

call_have_callee:
            if (callee.type != PS_T_OBJECT) {
                ctl->did_throw = 1;
                const char *prefix = "Call of non-object: ";
                char *target = ps_format_call_target_name(callee_node);
                if (target) {
                    size_t prefix_len = strlen(prefix);
                    size_t target_len = strlen(target);
                    char *msg = (char *)malloc(prefix_len + target_len + 1);
                    if (msg) {
                        memcpy(msg, prefix, prefix_len);
                        memcpy(msg + prefix_len, target, target_len);
                        msg[prefix_len + target_len] = '\0';
                        ctl->throw_value = ps_vm_make_error(vm, "TypeError", msg);
                        free(msg);
                    } else {
                        ctl->throw_value = ps_vm_make_error(vm, "TypeError", "Call of non-object");
                    }
                    free(target);
                } else {
                    ctl->throw_value = ps_vm_make_error(vm, "TypeError", "Call of non-object");
                }
                return ctl->throw_value;
            }

            PSFunction *func = ps_function_from_object(callee.as.object);
            if (!func) {
                ctl->did_throw = 1;
                const char *prefix = "Not a callable object: ";
                char *target = ps_format_call_target_name(callee_node);
                if (target) {
                    size_t prefix_len = strlen(prefix);
                    size_t target_len = strlen(target);
                    char *msg = (char *)malloc(prefix_len + target_len + 1);
                    if (msg) {
                        memcpy(msg, prefix, prefix_len);
                        memcpy(msg + prefix_len, target, target_len);
                        msg[prefix_len + target_len] = '\0';
                        ctl->throw_value = ps_vm_make_error(vm, "TypeError", msg);
                        free(msg);
                    } else {
                        ctl->throw_value = ps_vm_make_error(vm, "TypeError", "Not a callable object");
                    }
                    free(target);
                } else {
                    ctl->throw_value = ps_vm_make_error(vm, "TypeError", "Not a callable object");
                }
                return ctl->throw_value;
            }

            PSValue *args = NULL;
            PSValue stack_args[8];
            int args_heap = 0;
            if (!ps_eval_args(vm, env, node->as.call.args, node->as.call.argc,
                              stack_args, 8, &args, &args_heap, ctl)) {
                return ctl->did_throw ? ctl->throw_value : ps_value_undefined();
            }
#if PS_ENABLE_FAST_CALLS
            if (ps_fast_add_applicable(func, NULL, NULL) &&
                node->as.call.argc >= 2 &&
                args[0].type == PS_T_NUMBER &&
                args[1].type == PS_T_NUMBER) {
                double sum = args[0].as.number + args[1].as.number;
                if (args_heap) {
                    free(args);
                }
                return ps_value_number(sum);
            }
#endif
            int did_throw = 0;
            PSValue throw_value = ps_value_undefined();
            uint64_t prof_start = 0;
            int prof_enabled = (vm && vm->profile.enabled);
            if (prof_enabled) {
                prof_start = ps_eval_now_ms();
            }
            PSValue result = ps_eval_call_function(vm, env, callee.as.object, this_val,
                                                   (int)node->as.call.argc, args,
                                                   &did_throw, &throw_value);
            if (prof_enabled) {
                uint64_t elapsed = ps_eval_now_ms() - prof_start;
                ps_vm_profile_add(vm, func, elapsed);
            }
            if (args_heap) {
                free(args);
            }
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
            PSValue stack_args[8];
            int args_heap = 0;
            if (!ps_eval_args(vm, env, node->as.new_expr.args, node->as.new_expr.argc,
                              stack_args, 8, &args, &args_heap, ctl)) {
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
            if (args_heap) {
                free(args);
            }

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
