#include "ps_vm.h"
#include "ps_object.h"
#include "ps_array.h"
#include "ps_value.h"
#include "ps_string.h"
#include "ps_env.h"
#include "ps_function.h"
#include "ps_eval.h"
#include "ps_parser.h"
#include "ps_ast.h"
#include "ps_regexp.h"
#include "ps_config.h"
#include "ps_display.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <time.h>

static void ps_vm_profile_dump(PSVM *vm);

/* Forward declaration (implemented in io.c) */
void ps_io_init(PSVM *vm);
void ps_buffer_init(PSVM *vm);
void ps_event_init(PSVM *vm);
void ps_display_init(PSVM *vm);
#if PS_ENABLE_MODULE_FS
void ps_fs_init(PSVM *vm);
#endif
#if PS_ENABLE_MODULE_IMG
void ps_img_init(PSVM *vm);
#endif
static PSValue ps_native_date_to_string(PSVM *vm, PSValue this_val, int argc, PSValue *argv);
static PSValue ps_date_parse_iso(PSVM *vm, PSString *s);
static PSString *ps_date_format_utc(double ms_num);
static int64_t ps_to_int64(double v, int *ok);
static size_t ps_utf8_encode(uint32_t code, char *out);
static PSValue ps_native_is_finite(PSVM *vm, PSValue this_val, int argc, PSValue *argv);
static PSValue ps_native_is_nan(PSVM *vm, PSValue this_val, int argc, PSValue *argv);
static PSValue ps_native_parse_int(PSVM *vm, PSValue this_val, int argc, PSValue *argv);

static uint64_t ps_vm_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000ULL);
}
static PSValue ps_native_parse_float(PSVM *vm, PSValue this_val, int argc, PSValue *argv);
static PSValue ps_native_escape(PSVM *vm, PSValue this_val, int argc, PSValue *argv);
static PSValue ps_native_unescape(PSVM *vm, PSValue this_val, int argc, PSValue *argv);
static PSValue ps_date_utc_from_parts(PSVM *vm,
                                      int year,
                                      int month,
                                      int day,
                                      int hour,
                                      int minute,
                                      int second,
                                      int ms,
                                      int argc);
static int ps_date_compute_ms(PSVM *vm, int argc, PSValue *argv, double *out_ms);

static int ps_value_is_nan(double v) {
    return isnan(v);
}

static PSValue ps_native_empty(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    (void)argc;
    (void)argv;
    return ps_value_undefined();
}

static PSObject *ps_vm_error_proto_for_name(PSVM *vm, const char *name) {
    if (!vm || !name) return vm ? vm->error_proto : NULL;
    if (strcmp(name, "TypeError") == 0) return vm->type_error_proto;
    if (strcmp(name, "RangeError") == 0) return vm->range_error_proto;
    if (strcmp(name, "ReferenceError") == 0) return vm->reference_error_proto;
    if (strcmp(name, "SyntaxError") == 0) return vm->syntax_error_proto;
    if (strcmp(name, "EvalError") == 0) return vm->eval_error_proto;
    return vm->error_proto;
}

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} PSStackBuilder;

static int stack_builder_reserve(PSStackBuilder *b, size_t extra) {
    if (!b) return 0;
    size_t need = b->len + extra + 1;
    if (need <= b->cap) return 1;
    size_t new_cap = b->cap ? b->cap * 2 : 128;
    while (new_cap < need) {
        new_cap *= 2;
    }
    char *next = (char *)realloc(b->data, new_cap);
    if (!next) return 0;
    b->data = next;
    b->cap = new_cap;
    return 1;
}

static int stack_builder_append(PSStackBuilder *b, const char *s, size_t len) {
    if (!b || !s || len == 0) return 1;
    if (!stack_builder_reserve(b, len)) return 0;
    memcpy(b->data + b->len, s, len);
    b->len += len;
    b->data[b->len] = '\0';
    return 1;
}

static int stack_builder_append_cstr(PSStackBuilder *b, const char *s) {
    if (!s) return 1;
    return stack_builder_append(b, s, strlen(s));
}

static int stack_builder_append_size(PSStackBuilder *b, size_t value) {
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%zu", value);
    if (n <= 0) return 0;
    return stack_builder_append(b, buf, (size_t)n);
}

static int stack_builder_append_psstring(PSStackBuilder *b, const PSString *s) {
    if (!s || !s->utf8 || s->byte_len == 0) return 1;
    return stack_builder_append(b, s->utf8, s->byte_len);
}

static void stack_builder_add_frame(PSStackBuilder *b,
                                    const PSString *name,
                                    const char *path,
                                    size_t line,
                                    size_t column,
                                    int is_top) {
    const char *fallback = is_top ? "<global>" : "<anonymous>";
    stack_builder_append_cstr(b, "at ");
    if (name && name->byte_len > 0) {
        stack_builder_append_psstring(b, name);
    } else {
        stack_builder_append_cstr(b, fallback);
    }
    if ((path && path[0]) || (line > 0 && column > 0)) {
        stack_builder_append_cstr(b, " (");
        if (path && path[0]) {
            stack_builder_append_cstr(b, path);
            if (line > 0 && column > 0) {
                stack_builder_append_cstr(b, ":");
            }
        }
        if (line > 0 && column > 0) {
            stack_builder_append_size(b, line);
            stack_builder_append_cstr(b, ":");
            stack_builder_append_size(b, column);
        }
        stack_builder_append_cstr(b, ")");
    }
    stack_builder_append_cstr(b, "\n");
}

static PSString *ps_vm_build_stack(PSVM *vm) {
    if (!vm) return NULL;
    if (!vm->current_node && vm->stack_depth == 0) return NULL;
    PSStackBuilder b = {0};
    PSFunction *current_func = NULL;
    if (vm->current_callee) {
        current_func = ps_function_from_object(vm->current_callee);
    }
    size_t line = vm->current_node ? vm->current_node->line : 0;
    size_t column = vm->current_node ? vm->current_node->column : 0;
    const char *path = vm->current_node ? vm->current_node->source_path : NULL;
    stack_builder_add_frame(&b,
                            current_func ? current_func->name : NULL,
                            path,
                            line,
                            column,
                            1);
    for (size_t i = vm->stack_depth; i > 0; i--) {
        const PSStackFrame *frame = &vm->stack_frames[i - 1];
        stack_builder_add_frame(&b,
                                frame->function_name,
                                frame->source_path,
                                frame->line,
                                frame->column,
                                0);
    }
    if (!b.data || b.len == 0) {
        free(b.data);
        return NULL;
    }
    PSString *stack = ps_string_from_utf8(b.data, b.len);
    free(b.data);
    return stack;
}

static PSValue ps_vm_make_error_with_message_and_code(PSVM *vm,
                                                      const char *name,
                                                      PSString *message,
                                                      const char *code) {
    PSObject *proto = ps_vm_error_proto_for_name(vm, name);
    PSObject *obj = ps_object_new(proto ? proto : (vm ? vm->object_proto : NULL));
    if (!obj) return ps_value_undefined();
    ps_object_define(obj,
                     ps_string_from_cstr("name"),
                     ps_value_string(ps_string_from_cstr(name ? name : "Error")),
                     PS_ATTR_DONTENUM);
    ps_object_define(obj,
                     ps_string_from_cstr("message"),
                     ps_value_string(message ? message : ps_string_from_cstr("")),
                     PS_ATTR_DONTENUM);
    if (code && code[0]) {
        ps_object_define(obj,
                         ps_string_from_cstr("code"),
                         ps_value_string(ps_string_from_cstr(code)),
                         PS_ATTR_DONTENUM);
    }
    if (vm && vm->current_node && vm->current_node->line && vm->current_node->column) {
        ps_object_define(obj,
                         ps_string_from_cstr("line"),
                         ps_value_number((double)vm->current_node->line),
                         PS_ATTR_DONTENUM);
        ps_object_define(obj,
                         ps_string_from_cstr("column"),
                         ps_value_number((double)vm->current_node->column),
                         PS_ATTR_DONTENUM);
        if (vm->current_node->source_path) {
            ps_object_define(obj,
                             ps_string_from_cstr("file"),
                             ps_value_string(ps_string_from_cstr(vm->current_node->source_path)),
                             PS_ATTR_DONTENUM);
        }
    }
    PSString *stack = ps_vm_build_stack(vm);
    if (stack) {
        ps_object_define(obj,
                         ps_string_from_cstr("stack"),
                         ps_value_string(stack),
                         PS_ATTR_DONTENUM);
    }
    return ps_value_object(obj);
}

static PSValue ps_vm_make_error_with_message(PSVM *vm, const char *name, PSString *message) {
    return ps_vm_make_error_with_message_and_code(vm, name, message, NULL);
}

PSValue ps_vm_make_error(PSVM *vm, const char *name, const char *message) {
    PSString *msg = ps_string_from_cstr(message ? message : "");
    return ps_vm_make_error_with_message_and_code(vm, name, msg, NULL);
}

PSValue ps_vm_make_error_with_code(PSVM *vm, const char *name, const char *message, const char *code) {
    PSString *msg = ps_string_from_cstr(message ? message : "");
    return ps_vm_make_error_with_message_and_code(vm, name, msg, code);
}

void ps_vm_throw_type_error(PSVM *vm, const char *message) {
    if (!vm) return;
    const char *msg = message ? message : "";
    if (message && strcmp(message, "Invalid receiver") == 0) {
        PSFunction *func = vm->current_callee ? ps_function_from_object(vm->current_callee) : NULL;
        if (func && func->name && func->name->byte_len > 0) {
            size_t name_len = func->name->byte_len;
            const char *prefix = "Invalid receiver: ";
            size_t prefix_len = strlen(prefix);
            char *buf = (char *)malloc(prefix_len + name_len + 1);
            if (buf) {
                memcpy(buf, prefix, prefix_len);
                memcpy(buf + prefix_len, func->name->utf8, name_len);
                buf[prefix_len + name_len] = '\0';
                msg = buf;
            }
        }
    }
    vm->pending_throw = ps_vm_make_error_with_code(vm,
                                                   "TypeError",
                                                   msg,
                                                   "ERR_INVALID_ARG");
    if (msg != message && message && strcmp(message, "Invalid receiver") == 0) {
        free((void *)msg);
    }
    vm->has_pending_throw = 1;
}

static void ps_vm_throw_range_error(PSVM *vm, const char *message) {
    if (!vm) return;
    vm->pending_throw = ps_vm_make_error_with_code(vm,
                                                   "RangeError",
                                                   message ? message : "",
                                                   "ERR_OUT_OF_RANGE");
    vm->has_pending_throw = 1;
}

static void ps_vm_throw_syntax_error(PSVM *vm, const char *message) {
    if (!vm) return;
    vm->pending_throw = ps_vm_make_error(vm, "SyntaxError", message ? message : "");
    vm->has_pending_throw = 1;
}

static void ps_define_function_props(PSObject *fn, const char *name, int length) {
    if (!fn) return;
    PSFunction *func = ps_function_from_object(fn);
    if (func && name) {
        func->name = ps_string_from_cstr(name);
    }
    ps_object_define(fn,
                     ps_string_from_cstr("length"),
                     ps_value_number((double)length),
                     PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
    if (name) {
        ps_object_define(fn,
                         ps_string_from_cstr("name"),
                         ps_value_string(ps_string_from_cstr(name)),
                         PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
    }
}

static void ps_object_set_length(PSObject *obj, size_t len);
static PSValue ps_native_regexp_exec(PSVM *vm, PSValue this_val, int argc, PSValue *argv);

static void ps_error_apply_options(PSVM *vm, PSValue error_val, int argc, PSValue *argv) {
    (void)vm;
    if (argc < 2) return;
    if (error_val.type != PS_T_OBJECT || !error_val.as.object) return;
    if (argv[1].type != PS_T_OBJECT || !argv[1].as.object) return;
    int found = 0;
    PSValue cause_val = ps_object_get(argv[1].as.object, ps_string_from_cstr("cause"), &found);
    if (found) {
        ps_object_define(error_val.as.object,
                         ps_string_from_cstr("cause"),
                         cause_val,
                         PS_ATTR_DONTENUM);
    }
}

static PSValue ps_native_error_to_string(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)argc;
    (void)argv;
    if (this_val.type != PS_T_OBJECT || !this_val.as.object) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    PSObject *obj = this_val.as.object;
    int found = 0;
    PSValue name_val = ps_object_get(obj, ps_string_from_cstr("name"), &found);
    PSString *name = found ? ps_to_string(vm, name_val) : ps_string_from_cstr("Error");
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    found = 0;
    PSValue msg_val = ps_object_get(obj, ps_string_from_cstr("message"), &found);
    PSString *msg = found ? ps_to_string(vm, msg_val) : ps_string_from_cstr("");
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    if (!msg || ps_string_length(msg) == 0) {
        return ps_value_string(name);
    }
    PSString *sep = ps_string_from_cstr(": ");
    PSString *left = ps_string_concat(name, sep);
    return ps_value_string(ps_string_concat(left, msg));
}

static PSValue ps_native_error(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    PSString *message = ps_string_from_cstr("");
    if (argc > 0) {
        message = ps_to_string(vm, argv[0]);
    }
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    PSValue out = ps_vm_make_error_with_message(vm, "Error", message);
    ps_error_apply_options(vm, out, argc, argv);
    return out;
}

static PSValue ps_native_type_error(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    PSString *message = ps_string_from_cstr("");
    if (argc > 0) {
        message = ps_to_string(vm, argv[0]);
    }
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    PSValue out = ps_vm_make_error_with_message(vm, "TypeError", message);
    ps_error_apply_options(vm, out, argc, argv);
    return out;
}

static PSValue ps_native_reference_error(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    PSString *message = ps_string_from_cstr("");
    if (argc > 0) {
        message = ps_to_string(vm, argv[0]);
    }
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    PSValue out = ps_vm_make_error_with_message(vm, "ReferenceError", message);
    ps_error_apply_options(vm, out, argc, argv);
    return out;
}

static PSValue ps_native_syntax_error(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    PSString *message = ps_string_from_cstr("");
    if (argc > 0) {
        message = ps_to_string(vm, argv[0]);
    }
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    PSValue out = ps_vm_make_error_with_message(vm, "SyntaxError", message);
    ps_error_apply_options(vm, out, argc, argv);
    return out;
}

static PSValue ps_native_eval_error(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    PSString *message = ps_string_from_cstr("");
    if (argc > 0) {
        message = ps_to_string(vm, argv[0]);
    }
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    PSValue out = ps_vm_make_error_with_message(vm, "EvalError", message);
    ps_error_apply_options(vm, out, argc, argv);
    return out;
}

static PSValue ps_native_range_error(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    PSString *message = ps_string_from_cstr("");
    if (argc > 0) {
        message = ps_to_string(vm, argv[0]);
    }
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    PSValue out = ps_vm_make_error_with_message(vm, "RangeError", message);
    ps_error_apply_options(vm, out, argc, argv);
    return out;
}

static PSValue ps_native_object(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (argc == 0 || argv[0].type == PS_T_NULL || argv[0].type == PS_T_UNDEFINED) {
        PSObject *obj = ps_object_new(vm->object_proto);
        if (!obj) return ps_value_undefined();
        return ps_value_object(obj);
    }
    if (argv[0].type == PS_T_OBJECT) {
        return argv[0];
    }
    PSObject *wrapped = ps_vm_wrap_primitive(vm, &argv[0]);
    if (!wrapped) return ps_value_undefined();
    return ps_value_object(wrapped);
}

static PSValue ps_native_function(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    PSString *body = ps_string_from_cstr("");
    PSString **params = NULL;
    int param_count = (argc > 0) ? (argc - 1) : 0;

    if (argc > 0) {
        body = ps_to_string(vm, argv[argc - 1]);
        if (vm && vm->has_pending_throw) return ps_value_undefined();
    }

    if (param_count > 0) {
        params = calloc((size_t)param_count, sizeof(PSString *));
        if (!params) return ps_value_undefined();
        for (int i = 0; i < param_count; i++) {
            params[i] = ps_to_string(vm, argv[i]);
            if (vm && vm->has_pending_throw) {
                free(params);
                return ps_value_undefined();
            }
        }
    }

    const char *prefix = "function __ps_ctor(";
    const char *mid = "){";
    const char *suffix = "}";
    size_t params_len = 0;
    for (int i = 0; i < param_count; i++) {
        params_len += params[i] ? params[i]->byte_len : 0;
        if (i > 0) params_len += 1;
    }
    size_t total_len = strlen(prefix) + params_len + strlen(mid) +
                       (body ? body->byte_len : 0) + strlen(suffix) + 1;
    char *source = malloc(total_len);
    if (!source) {
        free(params);
        return ps_value_undefined();
    }
    char *cursor = source;
    memcpy(cursor, prefix, strlen(prefix));
    cursor += strlen(prefix);
    for (int i = 0; i < param_count; i++) {
        if (i > 0) *cursor++ = ',';
        if (params[i] && params[i]->byte_len > 0) {
            memcpy(cursor, params[i]->utf8, params[i]->byte_len);
            cursor += params[i]->byte_len;
        }
    }
    memcpy(cursor, mid, strlen(mid));
    cursor += strlen(mid);
    if (body && body->byte_len > 0) {
        memcpy(cursor, body->utf8, body->byte_len);
        cursor += body->byte_len;
    }
    memcpy(cursor, suffix, strlen(suffix));
    cursor += strlen(suffix);
    *cursor = '\0';

    PSAstNode *program = ps_parse_with_path(source, NULL);
    free(source);
    free(params);

    if (!program || program->as.list.count == 0 ||
        program->as.list.items[0]->kind != AST_FUNCTION_DECL) {
        ps_vm_throw_syntax_error(vm, "Invalid function source");
        return ps_value_undefined();
    }

    PSAstNode *decl = program->as.list.items[0];
    PSObject *fn = ps_function_new_script(decl->as.func_decl.params,
                                          decl->as.func_decl.param_defaults,
                                          decl->as.func_decl.param_count,
                                          decl->as.func_decl.body,
                                          vm ? vm->env : NULL);
    if (!fn) return ps_value_undefined();
    ps_function_setup(fn, vm->function_proto, vm->object_proto, NULL);
    ps_define_function_props(fn, "anonymous", (int)decl->as.func_decl.param_count);
    return ps_value_object(fn);
}

static PSValue ps_native_boolean(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    int value = 0;
    if (argc > 0) {
        value = ps_to_boolean(vm, argv[0]);
    }
    if (vm && vm->is_constructing &&
        this_val.type == PS_T_OBJECT && this_val.as.object &&
        this_val.as.object->kind == PS_OBJ_KIND_BOOLEAN &&
        !this_val.as.object->internal) {
        PSObject *obj = this_val.as.object;
        if (!obj->internal) {
            obj->internal = malloc(sizeof(PSValue));
        }
        if (obj->internal) {
            *((PSValue *)obj->internal) = ps_value_boolean(value);
        }
        return this_val;
    }
    return ps_value_boolean(value);
}

static PSValue ps_native_number(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    double value = 0.0;
    if (argc > 0) {
        value = ps_to_number(vm, argv[0]);
    }
    if (vm && vm->is_constructing &&
        this_val.type == PS_T_OBJECT && this_val.as.object &&
        this_val.as.object->kind == PS_OBJ_KIND_NUMBER &&
        !this_val.as.object->internal) {
        PSObject *obj = this_val.as.object;
        if (!obj->internal) {
            obj->internal = malloc(sizeof(PSValue));
        }
        if (obj->internal) {
            *((PSValue *)obj->internal) = ps_value_number(value);
        }
        return this_val;
    }
    return ps_value_number(value);
}

static PSValue ps_native_string(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    PSString *value = ps_string_from_cstr("");
    if (argc > 0) {
        value = ps_to_string(vm, argv[0]);
    }
    if (vm && vm->is_constructing &&
        this_val.type == PS_T_OBJECT && this_val.as.object &&
        this_val.as.object->kind == PS_OBJ_KIND_STRING &&
        !this_val.as.object->internal) {
        PSObject *obj = this_val.as.object;
        if (!obj->internal) {
            obj->internal = malloc(sizeof(PSValue));
        }
        if (obj->internal) {
            *((PSValue *)obj->internal) = ps_value_string(value);
        }
        ps_object_define(obj,
                         ps_string_from_cstr("length"),
                         ps_value_number((double)(value ? value->glyph_count : 0)),
                         PS_ATTR_READONLY | PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
        return this_val;
    }
    return ps_value_string(value);
}

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} PSCharBuffer;

static void ps_char_buffer_init(PSCharBuffer *b) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static void ps_char_buffer_free(PSCharBuffer *b) {
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static int ps_char_buffer_reserve(PSCharBuffer *b, size_t extra) {
    size_t needed = b->len + extra;
    if (needed <= b->cap) return 1;
    size_t cap = b->cap ? b->cap * 2 : 64;
    while (cap < needed) cap *= 2;
    char *next = realloc(b->data, cap);
    if (!next) return 0;
    b->data = next;
    b->cap = cap;
    return 1;
}

static int ps_char_buffer_append(PSCharBuffer *b, const char *src, size_t len) {
    if (!ps_char_buffer_reserve(b, len)) return 0;
    memcpy(b->data + b->len, src, len);
    b->len += len;
    return 1;
}

static int ps_char_buffer_append_char(PSCharBuffer *b, char c) {
    return ps_char_buffer_append(b, &c, 1);
}

static int ps_is_ascii_space_local(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static int ps_hex_value_local(unsigned char c) {
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'a' && c <= 'f') return (int)(c - 'a') + 10;
    if (c >= 'A' && c <= 'F') return (int)(c - 'A') + 10;
    return -1;
}

static int ps_radix_digit_value(unsigned char c) {
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'a' && c <= 'z') return (int)(c - 'a') + 10;
    if (c >= 'A' && c <= 'Z') return (int)(c - 'A') + 10;
    return -1;
}

static int ps_escape_passthrough(unsigned char c) {
    if ((c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9')) {
        return 1;
    }
    switch (c) {
        case '@':
        case '*':
        case '_':
        case '+':
        case '-':
        case '.':
        case '/':
            return 1;
        default:
            return 0;
    }
}

static PSValue ps_native_is_finite(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    double num = (argc > 0) ? ps_to_number(vm, argv[0]) : NAN;
    return ps_value_boolean(isfinite(num));
}

static PSValue ps_native_is_nan(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    double num = (argc > 0) ? ps_to_number(vm, argv[0]) : NAN;
    return ps_value_boolean(isnan(num));
}

static PSValue ps_native_parse_float(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (argc < 1) return ps_value_number(NAN);
    PSString *s = ps_to_string(vm, argv[0]);
    if (!s || s->byte_len == 0) return ps_value_number(NAN);

    size_t start = 0;
    while (start < s->byte_len &&
           ps_is_ascii_space_local((unsigned char)s->utf8[start])) {
        start++;
    }
    if (start >= s->byte_len) return ps_value_number(NAN);

    size_t len = s->byte_len - start;
    char *tmp = malloc(len + 1);
    if (!tmp) return ps_value_number(NAN);
    memcpy(tmp, s->utf8 + start, len);
    tmp[len] = '\0';

    char *endptr = NULL;
    double val = strtod(tmp, &endptr);
    if (!endptr || endptr == tmp) {
        free(tmp);
        return ps_value_number(NAN);
    }
    free(tmp);
    return ps_value_number(val);
}

static PSValue ps_native_parse_int(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (argc < 1) return ps_value_number(NAN);
    PSString *s = ps_to_string(vm, argv[0]);
    if (!s || s->byte_len == 0) return ps_value_number(NAN);

    int radix = 0;
    if (argc > 1 && argv[1].type != PS_T_UNDEFINED) {
        double r = ps_to_number(vm, argv[1]);
        if (!isnan(r) && isfinite(r)) {
            radix = (int)r;
        }
    }
    if (radix != 0 && (radix < 2 || radix > 36)) {
        return ps_value_number(NAN);
    }

    size_t i = 0;
    while (i < s->byte_len && ps_is_ascii_space_local((unsigned char)s->utf8[i])) i++;
    if (i >= s->byte_len) return ps_value_number(NAN);

    int sign = 1;
    if (s->utf8[i] == '+' || s->utf8[i] == '-') {
        if (s->utf8[i] == '-') sign = -1;
        i++;
    }

    if (radix == 0) {
        if (i + 1 < s->byte_len && s->utf8[i] == '0' &&
            (s->utf8[i + 1] == 'x' || s->utf8[i + 1] == 'X')) {
            radix = 16;
            i += 2;
        } else {
            radix = 10;
        }
    } else if (radix == 16) {
        if (i + 1 < s->byte_len && s->utf8[i] == '0' &&
            (s->utf8[i + 1] == 'x' || s->utf8[i + 1] == 'X')) {
            i += 2;
        }
    }

    int saw_digit = 0;
    double value = 0.0;
    for (; i < s->byte_len; i++) {
        int digit = ps_radix_digit_value((unsigned char)s->utf8[i]);
        if (digit < 0 || digit >= radix) break;
        saw_digit = 1;
        value = value * (double)radix + (double)digit;
    }
    if (!saw_digit) return ps_value_number(NAN);
    return ps_value_number((double)sign * value);
}

static PSValue ps_native_escape(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    PSString *s = (argc > 0) ? ps_to_string(vm, argv[0]) : ps_string_from_cstr("");
    if (!s || s->glyph_count == 0) {
        return ps_value_string(ps_string_from_cstr(""));
    }

    PSCharBuffer b;
    ps_char_buffer_init(&b);
    static const char hex[] = "0123456789ABCDEF";

    for (size_t i = 0; i < s->glyph_count; i++) {
        uint32_t code = ps_string_char_code_at(s, i);
        if (code <= 0x7F && ps_escape_passthrough((unsigned char)code)) {
            if (!ps_char_buffer_append_char(&b, (char)code)) {
                ps_char_buffer_free(&b);
                return ps_value_undefined();
            }
            continue;
        }

        if (code <= 0xFF) {
            char buf[3];
            buf[0] = '%';
            buf[1] = hex[(code >> 4) & 0xF];
            buf[2] = hex[code & 0xF];
            if (!ps_char_buffer_append(&b, buf, sizeof(buf))) {
                ps_char_buffer_free(&b);
                return ps_value_undefined();
            }
            continue;
        }

        if (code <= 0xFFFF) {
            char buf[6];
            buf[0] = '%';
            buf[1] = 'u';
            buf[2] = hex[(code >> 12) & 0xF];
            buf[3] = hex[(code >> 8) & 0xF];
            buf[4] = hex[(code >> 4) & 0xF];
            buf[5] = hex[code & 0xF];
            if (!ps_char_buffer_append(&b, buf, sizeof(buf))) {
                ps_char_buffer_free(&b);
                return ps_value_undefined();
            }
            continue;
        }

        uint32_t cp = code - 0x10000;
        uint32_t high = 0xD800 + (cp >> 10);
        uint32_t low = 0xDC00 + (cp & 0x3FF);
        char buf[6];
        buf[0] = '%';
        buf[1] = 'u';
        buf[2] = hex[(high >> 12) & 0xF];
        buf[3] = hex[(high >> 8) & 0xF];
        buf[4] = hex[(high >> 4) & 0xF];
        buf[5] = hex[high & 0xF];
        if (!ps_char_buffer_append(&b, buf, sizeof(buf))) {
            ps_char_buffer_free(&b);
            return ps_value_undefined();
        }
        buf[2] = hex[(low >> 12) & 0xF];
        buf[3] = hex[(low >> 8) & 0xF];
        buf[4] = hex[(low >> 4) & 0xF];
        buf[5] = hex[low & 0xF];
        if (!ps_char_buffer_append(&b, buf, sizeof(buf))) {
            ps_char_buffer_free(&b);
            return ps_value_undefined();
        }
    }

    PSString *out = ps_string_from_utf8(b.data ? b.data : "", b.len);
    ps_char_buffer_free(&b);
    return ps_value_string(out);
}

static PSValue ps_native_unescape(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    PSString *s = (argc > 0) ? ps_to_string(vm, argv[0]) : ps_string_from_cstr("");
    if (!s || s->byte_len == 0) {
        return ps_value_string(ps_string_from_cstr(""));
    }

    PSCharBuffer b;
    ps_char_buffer_init(&b);
    size_t i = 0;
    while (i < s->byte_len) {
        unsigned char c = (unsigned char)s->utf8[i];
        if (c == '%' && i + 1 < s->byte_len) {
            if (s->utf8[i + 1] == 'u' && i + 5 < s->byte_len) {
                int h1 = ps_hex_value_local((unsigned char)s->utf8[i + 2]);
                int h2 = ps_hex_value_local((unsigned char)s->utf8[i + 3]);
                int h3 = ps_hex_value_local((unsigned char)s->utf8[i + 4]);
                int h4 = ps_hex_value_local((unsigned char)s->utf8[i + 5]);
                if (h1 >= 0 && h2 >= 0 && h3 >= 0 && h4 >= 0) {
                    uint32_t code = (uint32_t)((h1 << 12) | (h2 << 8) | (h3 << 4) | h4);
                    i += 6;
                    if (code >= 0xD800 && code <= 0xDBFF &&
                        i + 5 < s->byte_len &&
                        s->utf8[i] == '%' && s->utf8[i + 1] == 'u') {
                        int l1 = ps_hex_value_local((unsigned char)s->utf8[i + 2]);
                        int l2 = ps_hex_value_local((unsigned char)s->utf8[i + 3]);
                        int l3 = ps_hex_value_local((unsigned char)s->utf8[i + 4]);
                        int l4 = ps_hex_value_local((unsigned char)s->utf8[i + 5]);
                        if (l1 >= 0 && l2 >= 0 && l3 >= 0 && l4 >= 0) {
                            uint32_t low = (uint32_t)((l1 << 12) | (l2 << 8) | (l3 << 4) | l4);
                            if (low >= 0xDC00 && low <= 0xDFFF) {
                                uint32_t cp = ((code - 0xD800) << 10) + (low - 0xDC00) + 0x10000;
                                char utf8[4];
                                size_t bytes = ps_utf8_encode(cp, utf8);
                                if (!ps_char_buffer_append(&b, utf8, bytes)) {
                                    ps_char_buffer_free(&b);
                                    return ps_value_undefined();
                                }
                                i += 6;
                                continue;
                            }
                        }
                    }
                    char utf8[4];
                    size_t bytes = ps_utf8_encode(code, utf8);
                    if (!ps_char_buffer_append(&b, utf8, bytes)) {
                        ps_char_buffer_free(&b);
                        return ps_value_undefined();
                    }
                    continue;
                }
            } else if (i + 2 < s->byte_len) {
                int h1 = ps_hex_value_local((unsigned char)s->utf8[i + 1]);
                int h2 = ps_hex_value_local((unsigned char)s->utf8[i + 2]);
                if (h1 >= 0 && h2 >= 0) {
                    uint32_t code = (uint32_t)((h1 << 4) | h2);
                    if (code <= 0x7F) {
                        char byte = (char)code;
                        if (!ps_char_buffer_append(&b, &byte, 1)) {
                            ps_char_buffer_free(&b);
                            return ps_value_undefined();
                        }
                    } else {
                        char utf8[4];
                        size_t bytes = ps_utf8_encode(code, utf8);
                        if (!ps_char_buffer_append(&b, utf8, bytes)) {
                            ps_char_buffer_free(&b);
                            return ps_value_undefined();
                        }
                    }
                    i += 3;
                    continue;
                }
            }
        }
        if (!ps_char_buffer_append_char(&b, (char)c)) {
            ps_char_buffer_free(&b);
            return ps_value_undefined();
        }
        i++;
    }

    PSString *out = ps_string_from_utf8(b.data ? b.data : "", b.len);
    ps_char_buffer_free(&b);
    return ps_value_string(out);
}

static PSValue ps_native_array(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    PSObject *obj = ps_object_new(vm->array_proto ? vm->array_proto : vm->object_proto);
    if (!obj) return ps_value_undefined();
    obj->kind = PS_OBJ_KIND_ARRAY;
    (void)ps_array_init(obj);
    if (argc == 1 &&
        (argv[0].type == PS_T_NUMBER ||
         (argv[0].type == PS_T_OBJECT && argv[0].as.object &&
          argv[0].as.object->kind == PS_OBJ_KIND_NUMBER))) {
        double num = ps_to_number(vm, argv[0]);
        if (vm && vm->has_pending_throw) return ps_value_undefined();
        if (!isfinite(num) || num < 0.0 || floor(num) != num || num > 4294967295.0) {
            char msg[96];
            snprintf(msg, sizeof(msg), "Invalid array length: %.15g", num);
            ps_vm_throw_range_error(vm, msg);
            return ps_value_undefined();
        }
        (void)ps_array_set_length_internal(obj, (size_t)num);
        return ps_value_object(obj);
    }

    for (int i = 0; i < argc; i++) {
        (void)ps_array_set_index(obj, (size_t)i, argv[i]);
    }
    (void)ps_array_set_length_internal(obj, (size_t)argc);
    return ps_value_object(obj);
}

static PSValue ps_native_date(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    if (this_val.type == PS_T_OBJECT && this_val.as.object &&
        this_val.as.object->kind == PS_OBJ_KIND_DATE) {
        PSObject *obj = this_val.as.object;
        double ms = 0.0;
        (void)ps_date_compute_ms(vm, argc, argv, &ms);
        if (!obj->internal) {
            obj->internal = malloc(sizeof(PSValue));
        }
        if (obj->internal) {
            *((PSValue *)obj->internal) = ps_value_number(ms);
        }
        return this_val;
    }

    double ms = 0.0;
    (void)ps_date_compute_ms(vm, argc, argv, &ms);
    return ps_value_string(ps_date_format_utc(ms));
}

typedef enum {
    PS_RE_NODE_EMPTY = 0,
    PS_RE_NODE_LITERAL,
    PS_RE_NODE_DOT,
    PS_RE_NODE_CLASS,
    PS_RE_NODE_ALT,
    PS_RE_NODE_REPEAT,
    PS_RE_NODE_GROUP,
    PS_RE_NODE_ANCHOR_START,
    PS_RE_NODE_ANCHOR_END,
    PS_RE_NODE_BACKREF,
    PS_RE_NODE_WORD_BOUNDARY,
    PS_RE_NODE_WORD_NOT_BOUNDARY
} PSRegexNodeType;

typedef struct {
    uint32_t start;
    uint32_t end;
} PSRegexRange;

typedef struct {
    int negate;
    PSRegexRange *ranges;
    size_t count;
} PSRegexClass;

typedef struct PSRegexNode {
    PSRegexNodeType type;
    struct PSRegexNode *next;
    union {
        uint32_t literal;
        PSRegexClass *cls;
        struct {
            struct PSRegexNode *left;
            struct PSRegexNode *right;
        } alt;
        struct {
            struct PSRegexNode *child;
            int min;
            int max;
        } rep;
        struct {
            struct PSRegexNode *child;
            int index;
        } group;
        int backref;
    } as;
} PSRegexNode;

typedef struct {
    int start;
    int end;
    int defined;
} PSRegexCapture;

typedef struct {
    PSString *source;
    size_t pos;
    size_t length;
    int error;
    int capture_count;
} PSRegexParser;

static PSRegexNode *ps_re_node_new(PSRegexNodeType type) {
    PSRegexNode *node = (PSRegexNode *)calloc(1, sizeof(PSRegexNode));
    if (!node) return NULL;
    node->type = type;
    node->next = NULL;
    return node;
}

static PSRegexClass *ps_re_class_new(void) {
    PSRegexClass *cls = (PSRegexClass *)calloc(1, sizeof(PSRegexClass));
    if (!cls) return NULL;
    cls->negate = 0;
    cls->ranges = NULL;
    cls->count = 0;
    return cls;
}

static void ps_re_class_add_range(PSRegexClass *cls, uint32_t start, uint32_t end) {
    if (!cls) return;
    if (start > end) {
        uint32_t tmp = start;
        start = end;
        end = tmp;
    }
    PSRegexRange *ranges = (PSRegexRange *)realloc(cls->ranges, sizeof(PSRegexRange) * (cls->count + 1));
    if (!ranges) return;
    cls->ranges = ranges;
    cls->ranges[cls->count].start = start;
    cls->ranges[cls->count].end = end;
    cls->count += 1;
}

static void ps_re_class_add_literal(PSRegexClass *cls, uint32_t ch) {
    ps_re_class_add_range(cls, ch, ch);
}

static void ps_re_class_add_digit(PSRegexClass *cls) {
    ps_re_class_add_range(cls, '0', '9');
}

static void ps_re_class_add_word(PSRegexClass *cls) {
    ps_re_class_add_range(cls, '0', '9');
    ps_re_class_add_range(cls, 'A', 'Z');
    ps_re_class_add_range(cls, 'a', 'z');
    ps_re_class_add_literal(cls, '_');
}

static void ps_re_class_add_space(PSRegexClass *cls) {
    ps_re_class_add_literal(cls, ' ');
    ps_re_class_add_literal(cls, '\t');
    ps_re_class_add_literal(cls, '\n');
    ps_re_class_add_literal(cls, '\r');
    ps_re_class_add_literal(cls, '\f');
    ps_re_class_add_literal(cls, '\v');
}

static uint32_t ps_re_peek(PSRegexParser *p) {
    if (!p || p->pos >= p->length) return 0;
    return ps_string_char_code_at(p->source, p->pos);
}

static uint32_t ps_re_next(PSRegexParser *p) {
    if (!p || p->pos >= p->length) return 0;
    return ps_string_char_code_at(p->source, p->pos++);
}

static int ps_re_parse_hex(PSRegexParser *p, int digits, uint32_t *out) {
    uint32_t value = 0;
    for (int i = 0; i < digits; i++) {
        uint32_t c = ps_re_peek(p);
        if (!c) return 0;
        int v = 0;
        if (c >= '0' && c <= '9') v = (int)(c - '0');
        else if (c >= 'a' && c <= 'f') v = (int)(c - 'a') + 10;
        else if (c >= 'A' && c <= 'F') v = (int)(c - 'A') + 10;
        else return 0;
        value = (value << 4) + (uint32_t)v;
        p->pos++;
    }
    if (out) *out = value;
    return 1;
}

static PSRegexNode *ps_re_parse_expression(PSRegexParser *p);

static PSRegexNode *ps_re_parse_escape(PSRegexParser *p,
                                       int in_class,
                                       PSRegexClass *cls,
                                       uint32_t *out_literal,
                                       int *out_backref,
                                       PSRegexNodeType *out_special) {
    if (!p) return NULL;
    uint32_t c = ps_re_next(p);
    if (!c) {
        p->error = 1;
        return NULL;
    }
    if (c >= '1' && c <= '9' && !in_class) {
        if (out_backref) *out_backref = (int)(c - '0');
        PSRegexNode *node = ps_re_node_new(PS_RE_NODE_BACKREF);
        if (node) {
            node->as.backref = (int)(c - '0');
        }
        return node;
    }
    if (!in_class) {
        if (c == 'b') {
            if (out_special) *out_special = PS_RE_NODE_WORD_BOUNDARY;
            return ps_re_node_new(PS_RE_NODE_WORD_BOUNDARY);
        }
        if (c == 'B') {
            if (out_special) *out_special = PS_RE_NODE_WORD_NOT_BOUNDARY;
            return ps_re_node_new(PS_RE_NODE_WORD_NOT_BOUNDARY);
        }
    }
    if (c == 'd' || c == 'D' || c == 'w' || c == 'W' || c == 's' || c == 'S') {
        if (in_class && cls) {
            int negate = (c == 'D' || c == 'W' || c == 'S');
            if (c == 'd' || c == 'D') ps_re_class_add_digit(cls);
            if (c == 'w' || c == 'W') ps_re_class_add_word(cls);
            if (c == 's' || c == 'S') ps_re_class_add_space(cls);
            if (negate) cls->negate = !cls->negate;
            return NULL;
        }
        if (!in_class) {
            PSRegexClass *tmp = ps_re_class_new();
            if (!tmp) return NULL;
            if (c == 'd' || c == 'D') ps_re_class_add_digit(tmp);
            if (c == 'w' || c == 'W') ps_re_class_add_word(tmp);
            if (c == 's' || c == 'S') ps_re_class_add_space(tmp);
            if (c == 'D' || c == 'W' || c == 'S') tmp->negate = 1;
            PSRegexNode *node = ps_re_node_new(PS_RE_NODE_CLASS);
            if (!node) return NULL;
            node->as.cls = tmp;
            return node;
        }
    }
    if (c == 'n') c = '\n';
    else if (c == 'r') c = '\r';
    else if (c == 't') c = '\t';
    else if (c == 'v') c = '\v';
    else if (c == 'f') c = '\f';
    else if (c == 'x') {
        uint32_t v = 0;
        if (ps_re_parse_hex(p, 2, &v)) c = v;
    } else if (c == 'u') {
        uint32_t v = 0;
        if (ps_re_parse_hex(p, 4, &v)) c = v;
    }
    if (out_literal) *out_literal = c;
    return NULL;
}

static PSRegexNode *ps_re_make_empty(void) {
    return ps_re_node_new(PS_RE_NODE_EMPTY);
}

static PSRegexNode *ps_re_parse_class(PSRegexParser *p) {
    PSRegexClass *cls = ps_re_class_new();
    if (!cls) return NULL;
    if (ps_re_peek(p) == '^') {
        ps_re_next(p);
        cls->negate = 1;
    }
    int have_prev = 0;
    uint32_t prev = 0;
    while (p->pos < p->length) {
        uint32_t c = ps_re_next(p);
        if (c == ']') break;
        if (c == '\\') {
            uint32_t lit = 0;
            ps_re_parse_escape(p, 1, cls, &lit, NULL, NULL);
            if (lit) {
                c = lit;
            } else {
                have_prev = 0;
                continue;
            }
        }
        if (c == '-' && have_prev && ps_re_peek(p) != 0 && ps_re_peek(p) != ']') {
            uint32_t end = ps_re_next(p);
            if (end == '\\') {
                uint32_t lit = 0;
                ps_re_parse_escape(p, 1, cls, &lit, NULL, NULL);
                if (lit) end = lit;
            }
            ps_re_class_add_range(cls, prev, end);
            have_prev = 0;
            continue;
        }
        if (have_prev) {
            ps_re_class_add_literal(cls, prev);
        }
        prev = c;
        have_prev = 1;
    }
    if (have_prev) {
        ps_re_class_add_literal(cls, prev);
    }
    PSRegexNode *node = ps_re_node_new(PS_RE_NODE_CLASS);
    if (!node) return NULL;
    node->as.cls = cls;
    return node;
}

static PSRegexNode *ps_re_parse_atom(PSRegexParser *p) {
    uint32_t c = ps_re_peek(p);
    if (!c) return NULL;
    if (c == '(') {
        ps_re_next(p);
        int index = ++p->capture_count;
        PSRegexNode *child = ps_re_parse_expression(p);
        if (ps_re_peek(p) != ')') {
            p->error = 1;
            return NULL;
        }
        ps_re_next(p);
        PSRegexNode *node = ps_re_node_new(PS_RE_NODE_GROUP);
        if (!node) return NULL;
        node->as.group.child = child ? child : ps_re_make_empty();
        node->as.group.index = index;
        return node;
    }
    if (c == '[') {
        ps_re_next(p);
        return ps_re_parse_class(p);
    }
    if (c == '.') {
        ps_re_next(p);
        return ps_re_node_new(PS_RE_NODE_DOT);
    }
    if (c == '^') {
        ps_re_next(p);
        return ps_re_node_new(PS_RE_NODE_ANCHOR_START);
    }
    if (c == '$') {
        ps_re_next(p);
        return ps_re_node_new(PS_RE_NODE_ANCHOR_END);
    }
    if (c == '\\') {
        ps_re_next(p);
        uint32_t lit = 0;
        int backref = 0;
        PSRegexNodeType special = PS_RE_NODE_EMPTY;
        PSRegexNode *node = ps_re_parse_escape(p, 0, NULL, &lit, &backref, &special);
        if (node && node->type == PS_RE_NODE_BACKREF) {
            return node;
        }
        if (node && (node->type == PS_RE_NODE_WORD_BOUNDARY ||
                     node->type == PS_RE_NODE_WORD_NOT_BOUNDARY)) {
            return node;
        }
        if (!lit && node) return node;
        PSRegexNode *lit_node = ps_re_node_new(PS_RE_NODE_LITERAL);
        if (!lit_node) return NULL;
        lit_node->as.literal = lit;
        return lit_node;
    }
    ps_re_next(p);
    PSRegexNode *lit = ps_re_node_new(PS_RE_NODE_LITERAL);
    if (!lit) return NULL;
    lit->as.literal = c;
    return lit;
}

static int ps_re_parse_number(PSRegexParser *p, int *out) {
    int value = 0;
    int got = 0;
    while (p->pos < p->length) {
        uint32_t c = ps_re_peek(p);
        if (c < '0' || c > '9') break;
        got = 1;
        value = value * 10 + (int)(c - '0');
        p->pos++;
    }
    if (!got) return 0;
    if (out) *out = value;
    return 1;
}

static PSRegexNode *ps_re_parse_term(PSRegexParser *p) {
    PSRegexNode *atom = ps_re_parse_atom(p);
    if (!atom) return NULL;
    uint32_t c = ps_re_peek(p);
    if (c == '*' || c == '+' || c == '?' || c == '{') {
        int min = 0;
        int max = -1;
        if (c == '*') {
            min = 0;
            max = -1;
            ps_re_next(p);
        } else if (c == '+') {
            min = 1;
            max = -1;
            ps_re_next(p);
        } else if (c == '?') {
            min = 0;
            max = 1;
            ps_re_next(p);
        } else if (c == '{') {
            size_t save = p->pos;
            ps_re_next(p);
            int m = 0;
            int n = -1;
            if (!ps_re_parse_number(p, &m)) {
                p->pos = save;
            } else {
                if (ps_re_peek(p) == ',') {
                    ps_re_next(p);
                    if (ps_re_parse_number(p, &n)) {
                        max = n;
                    } else {
                        max = -1;
                    }
                } else {
                    max = m;
                }
                if (ps_re_peek(p) != '}') {
                    p->error = 1;
                    return NULL;
                }
                ps_re_next(p);
                min = m;
            }
        }
        if (p->error) return NULL;
        if (c != '{' || (min != 0 || max != -1)) {
            PSRegexNode *rep = ps_re_node_new(PS_RE_NODE_REPEAT);
            if (!rep) return NULL;
            rep->as.rep.child = atom;
            rep->as.rep.min = min;
            rep->as.rep.max = max;
            return rep;
        }
    }
    return atom;
}

static PSRegexNode *ps_re_parse_sequence(PSRegexParser *p) {
    PSRegexNode *head = NULL;
    PSRegexNode *tail = NULL;
    while (p->pos < p->length) {
        uint32_t c = ps_re_peek(p);
        if (c == '|' || c == ')') break;
        PSRegexNode *term = ps_re_parse_term(p);
        if (!term) break;
        if (!head) {
            head = term;
            tail = term;
        } else {
            tail->next = term;
            tail = term;
        }
        while (tail && tail->next) {
            tail = tail->next;
        }
    }
    return head ? head : ps_re_make_empty();
}

static PSRegexNode *ps_re_parse_expression(PSRegexParser *p) {
    PSRegexNode *left = ps_re_parse_sequence(p);
    while (ps_re_peek(p) == '|') {
        ps_re_next(p);
        PSRegexNode *right = ps_re_parse_sequence(p);
        PSRegexNode *alt = ps_re_node_new(PS_RE_NODE_ALT);
        if (!alt) return NULL;
        alt->as.alt.left = left ? left : ps_re_make_empty();
        alt->as.alt.right = right ? right : ps_re_make_empty();
        left = alt;
    }
    return left;
}

static PSRegex *ps_re_compile(PSString *pattern, int global, int ignore_case) {
    if (!pattern) return NULL;
    PSRegexParser parser;
    parser.source = pattern;
    parser.pos = 0;
    parser.length = ps_string_length(pattern);
    parser.error = 0;
    parser.capture_count = 0;

    PSRegexNode *ast = ps_re_parse_expression(&parser);
    if (parser.error || parser.pos < parser.length) {
        return NULL;
    }
    PSRegex *re = (PSRegex *)calloc(1, sizeof(PSRegex));
    if (!re) return NULL;
    re->source = pattern;
    re->ast = ast ? ast : ps_re_make_empty();
    re->capture_count = parser.capture_count;
    re->global = global;
    re->ignore_case = ignore_case;
    return re;
}

static void ps_re_node_free(PSRegexNode *node) {
    while (node) {
        PSRegexNode *next = node->next;
        switch (node->type) {
            case PS_RE_NODE_CLASS:
                if (node->as.cls) {
                    free(node->as.cls->ranges);
                    free(node->as.cls);
                }
                break;
            case PS_RE_NODE_ALT:
                ps_re_node_free(node->as.alt.left);
                ps_re_node_free(node->as.alt.right);
                break;
            case PS_RE_NODE_REPEAT:
                ps_re_node_free(node->as.rep.child);
                break;
            case PS_RE_NODE_GROUP:
                ps_re_node_free(node->as.group.child);
                break;
            default:
                break;
        }
        free(node);
        node = next;
    }
}

void ps_regex_free(PSRegex *re) {
    if (!re) return;
    ps_re_node_free(re->ast);
    re->ast = NULL;
    free(re);
}

static int ps_re_is_word(uint32_t ch) {
    if (ch >= '0' && ch <= '9') return 1;
    if (ch >= 'A' && ch <= 'Z') return 1;
    if (ch >= 'a' && ch <= 'z') return 1;
    return ch == '_';
}

typedef struct {
    uint32_t upper;
    uint32_t lower;
} PSCaseFoldPair;

static const PSCaseFoldPair ps_casefold_pairs[] = {
    {0x0041, 0x0061}, {0x0042, 0x0062}, {0x0043, 0x0063}, {0x0044, 0x0064},
    {0x0045, 0x0065}, {0x0046, 0x0066}, {0x0047, 0x0067}, {0x0048, 0x0068},
    {0x0049, 0x0069}, {0x004A, 0x006A}, {0x004B, 0x006B}, {0x004C, 0x006C},
    {0x004D, 0x006D}, {0x004E, 0x006E}, {0x004F, 0x006F}, {0x0050, 0x0070},
    {0x0051, 0x0071}, {0x0052, 0x0072}, {0x0053, 0x0073}, {0x0054, 0x0074},
    {0x0055, 0x0075}, {0x0056, 0x0076}, {0x0057, 0x0077}, {0x0058, 0x0078},
    {0x0059, 0x0079}, {0x005A, 0x007A},

    {0x00C0, 0x00E0}, {0x00C1, 0x00E1}, {0x00C2, 0x00E2}, {0x00C3, 0x00E3},
    {0x00C4, 0x00E4}, {0x00C5, 0x00E5}, {0x00C6, 0x00E6}, {0x00C7, 0x00E7},
    {0x00C8, 0x00E8}, {0x00C9, 0x00E9}, {0x00CA, 0x00EA}, {0x00CB, 0x00EB},
    {0x00CC, 0x00EC}, {0x00CD, 0x00ED}, {0x00CE, 0x00EE}, {0x00CF, 0x00EF},
    {0x00D0, 0x00F0}, {0x00D1, 0x00F1}, {0x00D2, 0x00F2}, {0x00D3, 0x00F3},
    {0x00D4, 0x00F4}, {0x00D5, 0x00F5}, {0x00D6, 0x00F6}, {0x00D8, 0x00F8},
    {0x00D9, 0x00F9}, {0x00DA, 0x00FA}, {0x00DB, 0x00FB}, {0x00DC, 0x00FC},
    {0x00DD, 0x00FD}, {0x00DE, 0x00FE}, {0x0178, 0x00FF},

    {0x0100, 0x0101}, {0x0102, 0x0103}, {0x0104, 0x0105}, {0x0106, 0x0107},
    {0x0108, 0x0109}, {0x010A, 0x010B}, {0x010C, 0x010D}, {0x010E, 0x010F},
    {0x0110, 0x0111}, {0x0112, 0x0113}, {0x0114, 0x0115}, {0x0116, 0x0117},
    {0x0118, 0x0119}, {0x011A, 0x011B}, {0x011C, 0x011D}, {0x011E, 0x011F},
    {0x0120, 0x0121}, {0x0122, 0x0123}, {0x0124, 0x0125}, {0x0126, 0x0127},
    {0x0128, 0x0129}, {0x012A, 0x012B}, {0x012C, 0x012D}, {0x012E, 0x012F},
    {0x0130, 0x0069}, {0x0132, 0x0133}, {0x0134, 0x0135}, {0x0136, 0x0137},
    {0x0139, 0x013A}, {0x013B, 0x013C}, {0x013D, 0x013E}, {0x013F, 0x0140},
    {0x0141, 0x0142}, {0x0143, 0x0144}, {0x0145, 0x0146}, {0x0147, 0x0148},
    {0x014A, 0x014B}, {0x014C, 0x014D}, {0x014E, 0x014F}, {0x0150, 0x0151},
    {0x0152, 0x0153}, {0x0154, 0x0155}, {0x0156, 0x0157}, {0x0158, 0x0159},
    {0x015A, 0x015B}, {0x015C, 0x015D}, {0x015E, 0x015F}, {0x0160, 0x0161},
    {0x0162, 0x0163}, {0x0164, 0x0165}, {0x0166, 0x0167}, {0x0168, 0x0169},
    {0x016A, 0x016B}, {0x016C, 0x016D}, {0x016E, 0x016F}, {0x0170, 0x0171},
    {0x0172, 0x0173}, {0x0174, 0x0175}, {0x0176, 0x0177}, {0x0179, 0x017A},
    {0x017B, 0x017C}, {0x017D, 0x017E},

    {0x0181, 0x0253}, {0x0182, 0x0183}, {0x0184, 0x0185}, {0x0186, 0x0254},
    {0x0187, 0x0188}, {0x0189, 0x0256}, {0x018A, 0x0257}, {0x018B, 0x018C},
    {0x018E, 0x01DD}, {0x018F, 0x0259}, {0x0190, 0x025B}, {0x0191, 0x0192},
    {0x0193, 0x0260}, {0x0194, 0x0263}, {0x0196, 0x0269}, {0x0197, 0x0268},
    {0x0198, 0x0199}, {0x019C, 0x026F}, {0x019D, 0x0272}, {0x019F, 0x0275},
    {0x01A0, 0x01A1}, {0x01A2, 0x01A3}, {0x01A4, 0x01A5}, {0x01A6, 0x0280},
    {0x01A7, 0x01A8}, {0x01A9, 0x0283}, {0x01AC, 0x01AD}, {0x01AE, 0x0288},
    {0x01AF, 0x01B0}, {0x01B1, 0x028A}, {0x01B2, 0x028B}, {0x01B3, 0x01B4},
    {0x01B5, 0x01B6}, {0x01B7, 0x0292}, {0x01B8, 0x01B9}, {0x01BC, 0x01BD},
    {0x01C4, 0x01C6}, {0x01C7, 0x01C9}, {0x01CA, 0x01CC}, {0x01CD, 0x01CE},
    {0x01CF, 0x01D0}, {0x01D1, 0x01D2}, {0x01D3, 0x01D4}, {0x01D5, 0x01D6},
    {0x01D7, 0x01D8}, {0x01D9, 0x01DA}, {0x01DB, 0x01DC}, {0x01DE, 0x01DF},
    {0x01E0, 0x01E1}, {0x01E2, 0x01E3}, {0x01E4, 0x01E5}, {0x01E6, 0x01E7},
    {0x01E8, 0x01E9}, {0x01EA, 0x01EB}, {0x01EC, 0x01ED}, {0x01EE, 0x01EF},
    {0x01F1, 0x01F3}, {0x01F4, 0x01F5}, {0x01F6, 0x0195}, {0x01F7, 0x01BF},
    {0x01F8, 0x01F9}, {0x01FA, 0x01FB}, {0x01FC, 0x01FD}, {0x01FE, 0x01FF},
    {0x0200, 0x0201}, {0x0202, 0x0203}, {0x0204, 0x0205}, {0x0206, 0x0207},
    {0x0208, 0x0209}, {0x020A, 0x020B}, {0x020C, 0x020D}, {0x020E, 0x020F},
    {0x0210, 0x0211}, {0x0212, 0x0213}, {0x0214, 0x0215}, {0x0216, 0x0217},
    {0x0218, 0x0219}, {0x021A, 0x021B}, {0x021C, 0x021D}, {0x021E, 0x021F},
    {0x0220, 0x019E}, {0x0222, 0x0223}, {0x0224, 0x0225}, {0x0226, 0x0227},
    {0x0228, 0x0229}, {0x022A, 0x022B}, {0x022C, 0x022D}, {0x022E, 0x022F},
    {0x0230, 0x0231}, {0x0232, 0x0233}, {0x023A, 0x2C65}, {0x023B, 0x023C},
    {0x023D, 0x019A}, {0x023E, 0x2C66}, {0x0241, 0x0242}, {0x0243, 0x0180},
    {0x0244, 0x0289}, {0x0245, 0x028C}, {0x0246, 0x0247}, {0x0248, 0x0249},
    {0x024A, 0x024B}, {0x024C, 0x024D}, {0x024E, 0x024F},

    {0x0391, 0x03B1}, {0x0392, 0x03B2}, {0x0393, 0x03B3}, {0x0394, 0x03B4},
    {0x0395, 0x03B5}, {0x0396, 0x03B6}, {0x0397, 0x03B7}, {0x0398, 0x03B8},
    {0x0399, 0x03B9}, {0x039A, 0x03BA}, {0x039B, 0x03BB}, {0x039C, 0x03BC},
    {0x039D, 0x03BD}, {0x039E, 0x03BE}, {0x039F, 0x03BF}, {0x03A0, 0x03C0},
    {0x03A1, 0x03C1}, {0x03A3, 0x03C3}, {0x03A4, 0x03C4}, {0x03A5, 0x03C5},
    {0x03A6, 0x03C6}, {0x03A7, 0x03C7}, {0x03A8, 0x03C8}, {0x03A9, 0x03C9},

    {0x0410, 0x0430}, {0x0411, 0x0431}, {0x0412, 0x0432}, {0x0413, 0x0433},
    {0x0414, 0x0434}, {0x0415, 0x0435}, {0x0416, 0x0436}, {0x0417, 0x0437},
    {0x0418, 0x0438}, {0x0419, 0x0439}, {0x041A, 0x043A}, {0x041B, 0x043B},
    {0x041C, 0x043C}, {0x041D, 0x043D}, {0x041E, 0x043E}, {0x041F, 0x043F},
    {0x0420, 0x0440}, {0x0421, 0x0441}, {0x0422, 0x0442}, {0x0423, 0x0443},
    {0x0424, 0x0444}, {0x0425, 0x0445}, {0x0426, 0x0446}, {0x0427, 0x0447},
    {0x0428, 0x0448}, {0x0429, 0x0449}, {0x042A, 0x044A}, {0x042B, 0x044B},
    {0x042C, 0x044C}, {0x042D, 0x044D}, {0x042E, 0x044E}, {0x042F, 0x044F},
};

static uint32_t ps_unicode_simple_lower(uint32_t ch) {
    switch (ch) {
        case 0x03C2: return 0x03C3; /* Greek final sigma -> sigma */
        case 0x017F: return 0x0073; /* Latin long s -> s */
        case 0x212A: return 0x006B; /* Kelvin sign -> k */
        case 0x212B: return 0x00E5; /* Angstrom sign -> a with ring */
        case 0x1E9E: return 0x00DF; /* Latin capital sharp s -> sharp s */
        default: break;
    }
    for (size_t i = 0; i < sizeof(ps_casefold_pairs) / sizeof(ps_casefold_pairs[0]); i++) {
        if (ps_casefold_pairs[i].upper == ch) return ps_casefold_pairs[i].lower;
        if (ps_casefold_pairs[i].lower == ch) return ps_casefold_pairs[i].lower;
    }
    return ch;
}

static uint32_t ps_unicode_simple_upper(uint32_t ch) {
    switch (ch) {
        case 0x03C2: return 0x03A3; /* Greek final sigma -> Sigma */
        case 0x03C3: return 0x03A3; /* Greek sigma -> Sigma */
        case 0x017F: return 0x0053; /* Latin long s -> S */
        case 0x00DF: return 0x1E9E; /* sharp s -> Latin capital sharp s */
        default: break;
    }
    for (size_t i = 0; i < sizeof(ps_casefold_pairs) / sizeof(ps_casefold_pairs[0]); i++) {
        if (ps_casefold_pairs[i].lower == ch) return ps_casefold_pairs[i].upper;
        if (ps_casefold_pairs[i].upper == ch) return ps_casefold_pairs[i].upper;
    }
    return ch;
}

static int ps_re_char_equal(uint32_t a, uint32_t b, int ignore_case) {
    if (a == b) return 1;
    if (!ignore_case) return 0;
    return ps_unicode_simple_lower(a) == ps_unicode_simple_lower(b);
}

static int ps_re_class_match(PSRegexClass *cls, uint32_t ch, int ignore_case) {
    if (!cls) return 0;
    int matched = 0;
    uint32_t lower = ch;
    uint32_t upper = ch;
    if (ignore_case) {
        lower = ps_unicode_simple_lower(ch);
        upper = ps_unicode_simple_upper(ch);
    }
    for (size_t i = 0; i < cls->count; i++) {
        uint32_t start = cls->ranges[i].start;
        uint32_t end = cls->ranges[i].end;
        if (ch >= start && ch <= end) {
            matched = 1;
            break;
        }
        if (ignore_case) {
            if ((lower >= start && lower <= end) || (upper >= start && upper <= end)) {
                matched = 1;
                break;
            }
        }
    }
    return cls->negate ? !matched : matched;
}

static void ps_re_capture_copy(PSRegexCapture *dst, const PSRegexCapture *src, int count) {
    for (int i = 0; i < count; i++) {
        dst[i] = src[i];
    }
}

static int ps_re_match_node(PSRegexNode *node,
                            PSRegexNode *next,
                            PSString *input,
                            size_t pos,
                            int ignore_case,
                            PSRegexCapture *caps,
                            int cap_count,
                            size_t *out_pos);

static int ps_re_match_repeat(PSRegexNode *node,
                              PSRegexNode *next,
                              PSString *input,
                              size_t pos,
                              int ignore_case,
                              PSRegexCapture *caps,
                              int cap_count,
                              size_t *out_pos) {
    int min = node->as.rep.min;
    int max = node->as.rep.max;
    size_t len = ps_string_length(input);
    if (max < 0) max = (int)len;
    if (max < min) max = min;

    size_t *positions = (size_t *)calloc((size_t)max + 1, sizeof(size_t));
    PSRegexCapture *snapshots = (PSRegexCapture *)calloc((size_t)(max + 1) * (size_t)cap_count,
                                                         sizeof(PSRegexCapture));
    if (!positions || !snapshots) {
        free(positions);
        free(snapshots);
        return 0;
    }
    positions[0] = pos;
    ps_re_capture_copy(snapshots, caps, cap_count);

    int count = 0;
    while (count < max) {
        size_t new_pos = 0;
        PSRegexCapture *state = snapshots + (size_t)count * (size_t)cap_count;
        PSRegexCapture *next_state = snapshots + (size_t)(count + 1) * (size_t)cap_count;
        ps_re_capture_copy(next_state, state, cap_count);
        if (!ps_re_match_node(node->as.rep.child, NULL, input, positions[count], ignore_case,
                              next_state, cap_count, &new_pos)) {
            break;
        }
        if (new_pos == positions[count]) {
            break;
        }
        positions[count + 1] = new_pos;
        count++;
    }

    for (int i = count; i >= min; i--) {
        PSRegexCapture *state = snapshots + (size_t)i * (size_t)cap_count;
        ps_re_capture_copy(caps, state, cap_count);
        size_t new_pos = 0;
        if (ps_re_match_node(node->next, next, input, positions[i], ignore_case,
                             caps, cap_count, &new_pos)) {
            if (out_pos) *out_pos = new_pos;
            free(positions);
            free(snapshots);
            return 1;
        }
    }

    free(positions);
    free(snapshots);
    return 0;
}

static int ps_re_match_node(PSRegexNode *node,
                            PSRegexNode *next,
                            PSString *input,
                            size_t pos,
                            int ignore_case,
                            PSRegexCapture *caps,
                            int cap_count,
                            size_t *out_pos) {
    if (!node) {
        if (next) {
            return ps_re_match_node(next, NULL, input, pos, ignore_case, caps, cap_count, out_pos);
        }
        if (out_pos) *out_pos = pos;
        return 1;
    }
    size_t len = ps_string_length(input);
    switch (node->type) {
        case PS_RE_NODE_EMPTY:
            return ps_re_match_node(node->next, next, input, pos, ignore_case, caps, cap_count, out_pos);
        case PS_RE_NODE_LITERAL:
            if (pos >= len) return 0;
            if (!ps_re_char_equal(ps_string_char_code_at(input, pos), node->as.literal, ignore_case)) return 0;
            return ps_re_match_node(node->next, next, input, pos + 1, ignore_case, caps, cap_count, out_pos);
        case PS_RE_NODE_DOT: {
            if (pos >= len) return 0;
            uint32_t ch = ps_string_char_code_at(input, pos);
            if (ch == '\n' || ch == '\r') return 0;
            return ps_re_match_node(node->next, next, input, pos + 1, ignore_case, caps, cap_count, out_pos);
        }
        case PS_RE_NODE_CLASS:
            if (pos >= len) return 0;
            if (!ps_re_class_match(node->as.cls, ps_string_char_code_at(input, pos), ignore_case)) return 0;
            return ps_re_match_node(node->next, next, input, pos + 1, ignore_case, caps, cap_count, out_pos);
        case PS_RE_NODE_ANCHOR_START:
            if (pos != 0) return 0;
            return ps_re_match_node(node->next, next, input, pos, ignore_case, caps, cap_count, out_pos);
        case PS_RE_NODE_ANCHOR_END:
            if (pos != len) return 0;
            return ps_re_match_node(node->next, next, input, pos, ignore_case, caps, cap_count, out_pos);
        case PS_RE_NODE_WORD_BOUNDARY:
        case PS_RE_NODE_WORD_NOT_BOUNDARY: {
            int prev = 0;
            int cur = 0;
            if (pos > 0) prev = ps_re_is_word(ps_string_char_code_at(input, pos - 1));
            if (pos < len) cur = ps_re_is_word(ps_string_char_code_at(input, pos));
            int boundary = (prev != cur);
            if ((node->type == PS_RE_NODE_WORD_BOUNDARY && !boundary) ||
                (node->type == PS_RE_NODE_WORD_NOT_BOUNDARY && boundary)) {
                return 0;
            }
            return ps_re_match_node(node->next, next, input, pos, ignore_case, caps, cap_count, out_pos);
        }
        case PS_RE_NODE_BACKREF: {
            int idx = node->as.backref;
            if (idx <= 0 || idx >= cap_count) return 0;
            if (!caps[idx].defined) {
                return ps_re_match_node(node->next, next, input, pos, ignore_case, caps, cap_count, out_pos);
            }
            size_t start = (size_t)caps[idx].start;
            size_t end = (size_t)caps[idx].end;
            size_t clen = end > start ? (end - start) : 0;
            if (pos + clen > len) return 0;
            for (size_t i = 0; i < clen; i++) {
                uint32_t a = ps_string_char_code_at(input, start + i);
                uint32_t b = ps_string_char_code_at(input, pos + i);
                if (!ps_re_char_equal(a, b, ignore_case)) return 0;
            }
            return ps_re_match_node(node->next, next, input, pos + clen, ignore_case, caps, cap_count, out_pos);
        }
        case PS_RE_NODE_GROUP: {
            int idx = node->as.group.index;
            PSRegexCapture saved = {0};
            if (idx >= 0 && idx < cap_count) {
                saved = caps[idx];
                caps[idx].defined = 1;
                caps[idx].start = (int)pos;
                caps[idx].end = (int)pos;
            }
            size_t child_end = 0;
            if (ps_re_match_node(node->as.group.child, NULL, input, pos, ignore_case,
                                 caps, cap_count, &child_end)) {
                if (idx >= 0 && idx < cap_count) {
                    caps[idx].end = (int)child_end;
                }
                if (ps_re_match_node(node->next, next, input, child_end, ignore_case,
                                     caps, cap_count, out_pos)) {
                    return 1;
                }
            }
            if (idx >= 0 && idx < cap_count) {
                caps[idx] = saved;
            }
            return 0;
        }
        case PS_RE_NODE_ALT: {
            PSRegexNode *cont = node->next ? node->next : next;
            PSRegexCapture *tmp = (PSRegexCapture *)calloc((size_t)cap_count, sizeof(PSRegexCapture));
            if (!tmp) return 0;
            ps_re_capture_copy(tmp, caps, cap_count);
            size_t new_pos = 0;
            if (ps_re_match_node(node->as.alt.left, cont, input, pos, ignore_case,
                                 tmp, cap_count, &new_pos)) {
                ps_re_capture_copy(caps, tmp, cap_count);
                free(tmp);
                if (out_pos) *out_pos = new_pos;
                return 1;
            }
            ps_re_capture_copy(tmp, caps, cap_count);
            if (ps_re_match_node(node->as.alt.right, cont, input, pos, ignore_case,
                                 tmp, cap_count, &new_pos)) {
                ps_re_capture_copy(caps, tmp, cap_count);
                free(tmp);
                if (out_pos) *out_pos = new_pos;
                return 1;
            }
            free(tmp);
            return 0;
        }
        case PS_RE_NODE_REPEAT:
            return ps_re_match_repeat(node, next, input, pos, ignore_case, caps, cap_count, out_pos);
        default:
            return 0;
    }
}

static PSValue ps_native_regexp(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    PSString *pattern = ps_string_from_cstr("");
    int flag_g = 0;
    int flag_i = 0;
    if (argc > 0) {
        if (argv[0].type == PS_T_OBJECT && argv[0].as.object &&
            argv[0].as.object->kind == PS_OBJ_KIND_REGEXP &&
            argv[0].as.object->internal) {
            PSRegex *src = (PSRegex *)argv[0].as.object->internal;
            if (src && src->source) {
                pattern = src->source;
            }
            if (argc < 2 || argv[1].type == PS_T_UNDEFINED) {
                flag_g = src ? src->global : 0;
                flag_i = src ? src->ignore_case : 0;
            }
        } else {
            pattern = ps_to_string(vm, argv[0]);
        }
    }
    if (argc > 1 && argv[1].type != PS_T_UNDEFINED) {
        PSString *flags = ps_to_string(vm, argv[1]);
        for (size_t i = 0; i < flags->byte_len; i++) {
            if (flags->utf8[i] == 'g') flag_g = 1;
            else if (flags->utf8[i] == 'i') flag_i = 1;
            else {
                ps_vm_throw_syntax_error(vm, "Invalid flags");
                return ps_value_undefined();
            }
        }
    }

    PSRegex *compiled = ps_re_compile(pattern, flag_g, flag_i);
    if (!compiled) {
        const char *flags = flag_g && flag_i ? "gi" : (flag_g ? "g" : (flag_i ? "i" : ""));
        size_t pat_len = pattern ? pattern->byte_len : 0;
        size_t flags_len = strlen(flags);
        size_t total = sizeof("Invalid regular expression: //") - 1 + pat_len + flags_len;
        char *msg = (char *)malloc(total + 1);
        if (msg) {
            size_t pos = 0;
            const char *prefix = "Invalid regular expression: /";
            size_t prefix_len = strlen(prefix);
            memcpy(msg + pos, prefix, prefix_len);
            pos += prefix_len;
            if (pattern && pattern->utf8 && pat_len > 0) {
                memcpy(msg + pos, pattern->utf8, pat_len);
                pos += pat_len;
            }
            msg[pos++] = '/';
            if (flags_len > 0) {
                memcpy(msg + pos, flags, flags_len);
                pos += flags_len;
            }
            msg[pos] = '\0';
            ps_vm_throw_syntax_error(vm, msg);
            free(msg);
        } else {
            ps_vm_throw_syntax_error(vm, "Invalid regular expression");
        }
        return ps_value_undefined();
    }

    if (this_val.type == PS_T_OBJECT && this_val.as.object) {
        PSObject *obj = this_val.as.object;
        obj->kind = PS_OBJ_KIND_REGEXP;
        obj->internal = compiled;
        ps_object_put(obj, ps_string_from_cstr("source"), ps_value_string(pattern));
        ps_object_put(obj, ps_string_from_cstr("global"), ps_value_boolean(flag_g));
        ps_object_put(obj, ps_string_from_cstr("ignoreCase"), ps_value_boolean(flag_i));
        ps_object_put(obj, ps_string_from_cstr("lastIndex"), ps_value_number(0.0));
        return this_val;
    }

    PSObject *obj = ps_object_new(vm->regexp_proto ? vm->regexp_proto : vm->object_proto);
    if (!obj) return ps_value_undefined();
    obj->kind = PS_OBJ_KIND_REGEXP;
    obj->internal = compiled;
    ps_object_put(obj, ps_string_from_cstr("source"), ps_value_string(pattern));
    ps_object_put(obj, ps_string_from_cstr("global"), ps_value_boolean(flag_g));
    ps_object_put(obj, ps_string_from_cstr("ignoreCase"), ps_value_boolean(flag_i));
    ps_object_put(obj, ps_string_from_cstr("lastIndex"), ps_value_number(0.0));
    return ps_value_object(obj);
}

static const char *ps_object_tag(const PSObject *obj) {
    if (!obj) return "Object";
    switch (obj->kind) {
        case PS_OBJ_KIND_FUNCTION: return "Function";
        case PS_OBJ_KIND_BOOLEAN: return "Boolean";
        case PS_OBJ_KIND_NUMBER: return "Number";
        case PS_OBJ_KIND_STRING: return "String";
        case PS_OBJ_KIND_ARRAY: return "Array";
        case PS_OBJ_KIND_DATE: return "Date";
        case PS_OBJ_KIND_REGEXP: return "RegExp";
        default: return "Object";
    }
}

static int ps_string_bytes_equal(const PSString *a, const PSString *b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->byte_len != b->byte_len) return 0;
    return memcmp(a->utf8, b->utf8, a->byte_len) == 0;
}

static PSProperty *ps_object_find_own_prop(PSObject *obj, const PSString *name) {
    if (!obj || !name) return NULL;
    for (PSProperty *p = obj->props; p; p = p->next) {
        if (ps_string_bytes_equal(p->name, name)) return p;
    }
    return NULL;
}

static int ps_object_call_method(PSVM *vm, PSObject *obj, const char *name, PSValue *out) {
    int found = 0;
    PSValue method = ps_object_get(obj, ps_string_from_cstr(name), &found);
    if (!found || method.type != PS_T_OBJECT || !method.as.object ||
        method.as.object->kind != PS_OBJ_KIND_FUNCTION) {
        if (name && name[0]) {
            char msg[96];
            snprintf(msg, sizeof(msg), "Not a callable object: %s", name);
            ps_vm_throw_type_error(vm, msg);
        } else {
            ps_vm_throw_type_error(vm, "Not a callable object");
        }
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
        return 0;
    }
    if (out) *out = result;
    return 1;
}

static PSValue ps_native_object_to_string(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)argc;
    (void)argv;
    if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    if (this_val.type != PS_T_OBJECT || !this_val.as.object) {
        return ps_value_string(ps_string_from_cstr("[object Object]"));
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "[object %s]", ps_object_tag(this_val.as.object));
    return ps_value_string(ps_string_from_cstr(buf));
}

static PSValue ps_native_object_to_locale_string(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)argc;
    (void)argv;
    if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    if (this_val.type != PS_T_OBJECT || !this_val.as.object) {
        PSObject *wrapped = ps_vm_wrap_primitive(vm, &this_val);
        if (!wrapped) return ps_value_undefined();
        this_val = ps_value_object(wrapped);
    }
    PSValue result = ps_value_undefined();
    if (!ps_object_call_method(vm, this_val.as.object, "toString", &result)) {
        return ps_value_undefined();
    }
    return result;
}

static PSValue ps_native_has_own_property(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    if (this_val.type != PS_T_OBJECT || !this_val.as.object) {
        if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
            ps_vm_throw_type_error(vm, "Invalid receiver");
            return ps_value_undefined();
        }
        PSObject *wrapped = ps_vm_wrap_primitive(vm, &this_val);
        if (!wrapped) return ps_value_boolean(0);
        this_val = ps_value_object(wrapped);
    }
    PSString *name = ps_string_from_cstr("undefined");
    if (argc > 0) {
        name = ps_to_string(vm, argv[0]);
    }
    return ps_value_boolean(ps_object_has_own(this_val.as.object, name));
}

static PSValue ps_native_object_property_is_enumerable(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    if (this_val.type != PS_T_OBJECT || !this_val.as.object) {
        if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
            ps_vm_throw_type_error(vm, "Invalid receiver");
            return ps_value_undefined();
        }
        PSObject *wrapped = ps_vm_wrap_primitive(vm, &this_val);
        if (!wrapped) return ps_value_boolean(0);
        this_val = ps_value_object(wrapped);
    }
    PSString *name = ps_string_from_cstr("undefined");
    if (argc > 0) {
        name = ps_to_string(vm, argv[0]);
    }
    PSProperty *prop = ps_object_find_own_prop(this_val.as.object, name);
    if (!prop) return ps_value_boolean(0);
    return ps_value_boolean((prop->attrs & PS_ATTR_DONTENUM) == 0);
}

static PSValue ps_native_object_value_of(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)argc;
    (void)argv;
    if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    return this_val;
}

static PSValue ps_native_object_is_prototype_of(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    if (this_val.type != PS_T_OBJECT || !this_val.as.object) {
        if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
            ps_vm_throw_type_error(vm, "Invalid receiver");
            return ps_value_undefined();
        }
        PSObject *wrapped = ps_vm_wrap_primitive(vm, &this_val);
        if (!wrapped) return ps_value_boolean(0);
        this_val = ps_value_object(wrapped);
    }
    if (argc < 1 || argv[0].type != PS_T_OBJECT || !argv[0].as.object) {
        return ps_value_boolean(0);
    }
    PSObject *proto = argv[0].as.object->prototype;
    while (proto) {
        if (proto == this_val.as.object) {
            return ps_value_boolean(1);
        }
        proto = proto->prototype;
    }
    return ps_value_boolean(0);
}

static int ps_object_has_in_proto_chain(PSObject *obj, PSObject *proto) {
    for (PSObject *cur = proto; cur; cur = cur->prototype) {
        if (cur == obj) return 1;
    }
    return 0;
}

static PSValue ps_native_object_get_prototype_of(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (argc < 1) {
        ps_vm_throw_type_error(vm, "Object.getPrototypeOf expects (obj)");
        return ps_value_undefined();
    }
    PSValue val = argv[0];
    if (val.type == PS_T_NULL || val.type == PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Object.getPrototypeOf expects (obj)");
        return ps_value_undefined();
    }
    if (val.type != PS_T_OBJECT || !val.as.object) {
        PSObject *wrapped = ps_vm_wrap_primitive(vm, &val);
        if (!wrapped) return ps_value_undefined();
        val = ps_value_object(wrapped);
    }
    PSObject *proto = val.as.object->prototype;
    if (!proto) return ps_value_null();
    return ps_value_object(proto);
}

static PSValue ps_native_object_set_prototype_of(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (argc < 2) {
        ps_vm_throw_type_error(vm, "Object.setPrototypeOf expects (obj, proto)");
        return ps_value_undefined();
    }
    if (argv[0].type != PS_T_OBJECT || !argv[0].as.object) {
        ps_vm_throw_type_error(vm, "Object.setPrototypeOf expects (obj, proto)");
        return ps_value_undefined();
    }
    PSObject *obj = argv[0].as.object;
    PSObject *proto = NULL;
    if (argv[1].type == PS_T_NULL) {
        proto = NULL;
    } else if (argv[1].type == PS_T_OBJECT && argv[1].as.object) {
        proto = argv[1].as.object;
    } else {
        ps_vm_throw_type_error(vm, "Object.setPrototypeOf expects (obj, proto)");
        return ps_value_undefined();
    }
    if (proto == obj || (proto && ps_object_has_in_proto_chain(obj, proto))) {
        ps_vm_throw_type_error(vm, "Prototype cycle is not allowed");
        return ps_value_undefined();
    }
    obj->prototype = proto;
    return argv[0];
}

static PSValue ps_native_object_create(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (argc < 1) {
        ps_vm_throw_type_error(vm, "Object.create expects (proto)");
        return ps_value_undefined();
    }
    if (argc > 1 && argv[1].type != PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Object.create properties not supported");
        return ps_value_undefined();
    }
    PSObject *proto = NULL;
    if (argv[0].type == PS_T_NULL) {
        proto = NULL;
    } else if (argv[0].type == PS_T_OBJECT && argv[0].as.object) {
        proto = argv[0].as.object;
    } else {
        ps_vm_throw_type_error(vm, "Object.create expects (proto)");
        return ps_value_undefined();
    }
    PSObject *obj = ps_object_new(proto);
    if (!obj) return ps_value_undefined();
    return ps_value_object(obj);
}

static PSValue ps_native_boolean_to_string(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)argc;
    (void)argv;
    if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    if (this_val.type == PS_T_BOOLEAN) {
        return ps_value_string(ps_string_from_cstr(this_val.as.boolean ? "true" : "false"));
    }
    if (this_val.type == PS_T_OBJECT && this_val.as.object &&
        this_val.as.object->kind == PS_OBJ_KIND_BOOLEAN &&
        this_val.as.object->internal) {
        PSValue *inner = (PSValue *)this_val.as.object->internal;
        if (inner->type == PS_T_BOOLEAN) {
            return ps_value_string(ps_string_from_cstr(inner->as.boolean ? "true" : "false"));
        }
    }
    ps_vm_throw_type_error(vm, "Invalid receiver");
    return ps_value_undefined();
}

static PSValue ps_native_boolean_value_of(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)argc;
    (void)argv;
    if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    if (this_val.type == PS_T_OBJECT && this_val.as.object &&
        this_val.as.object->kind == PS_OBJ_KIND_BOOLEAN &&
        this_val.as.object->internal) {
        return *((PSValue *)this_val.as.object->internal);
    }
    if (this_val.type == PS_T_BOOLEAN) {
        return this_val;
    }
    ps_vm_throw_type_error(vm, "Invalid receiver");
    return ps_value_undefined();
}

static size_t ps_utf8_encode(uint32_t code, char *out) {
    if (code <= 0x7F) {
        out[0] = (char)code;
        return 1;
    }
    if (code <= 0x7FF) {
        out[0] = (char)(0xC0 | ((code >> 6) & 0x1F));
        out[1] = (char)(0x80 | (code & 0x3F));
        return 2;
    }
    if (code <= 0xFFFF) {
        out[0] = (char)(0xE0 | ((code >> 12) & 0x0F));
        out[1] = (char)(0x80 | ((code >> 6) & 0x3F));
        out[2] = (char)(0x80 | (code & 0x3F));
        return 3;
    }
    if (code <= 0x10FFFF) {
        out[0] = (char)(0xF0 | ((code >> 18) & 0x07));
        out[1] = (char)(0x80 | ((code >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((code >> 6) & 0x3F));
        out[3] = (char)(0x80 | (code & 0x3F));
        return 4;
    }
    return 0;
}

static PSValue ps_native_string_from_char_code(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (argc <= 0) {
        return ps_value_string(ps_string_from_cstr(""));
    }
    size_t cap = (size_t)argc * 4;
    if (cap == 0) cap = 1;
    char *buf = malloc(cap);
    if (!buf) return ps_value_undefined();
    size_t len = 0;
    for (int i = 0; i < argc; i++) {
        double num = ps_to_number(vm, argv[i]);
        if (vm && vm->has_pending_throw) {
            free(buf);
            return ps_value_undefined();
        }
        int64_t n = 0;
        if (!isnan(num) && !isinf(num)) {
            n = (int64_t)num;
        }
        uint32_t code = (uint32_t)(n & 0xFFFF);
        char tmp[4];
        size_t bytes = ps_utf8_encode(code, tmp);
        if (bytes == 0) continue;
        if (len + bytes > cap) {
            size_t next_cap = cap * 2;
            if (next_cap < len + bytes) next_cap = len + bytes;
            char *next = realloc(buf, next_cap);
            if (!next) {
                free(buf);
                return ps_value_undefined();
            }
            buf = next;
            cap = next_cap;
        }
        memcpy(buf + len, tmp, bytes);
        len += bytes;
    }
    PSString *out = ps_string_from_utf8(buf, len);
    free(buf);
    if (!out) return ps_value_undefined();
    return ps_value_string(out);
}

static PSString *ps_number_to_string_radix(double num, int radix) {
    if (isnan(num)) return ps_string_from_cstr("NaN");
    if (isinf(num)) return ps_string_from_cstr(num < 0 ? "-Infinity" : "Infinity");
    if (num == 0.0) return ps_string_from_cstr("0");

    int negative = (num < 0.0);
    double abs = fabs(num);
    double intpart = floor(abs);
    double frac = abs - intpart;

    size_t cap = 32;
    char *int_digits = malloc(cap);
    size_t int_len = 0;
    if (!int_digits) return ps_string_from_cstr("");
    if (intpart == 0.0) {
        int_digits[int_len++] = '0';
    } else {
        while (intpart >= 1.0) {
            double rem = fmod(intpart, (double)radix);
            int digit = (int)rem;
            intpart = floor(intpart / (double)radix);
            if (int_len >= cap) {
                cap *= 2;
                char *next = realloc(int_digits, cap);
                if (!next) {
                    free(int_digits);
                    return ps_string_from_cstr("");
                }
                int_digits = next;
            }
            int_digits[int_len++] = (digit < 10) ? (char)('0' + digit) : (char)('a' + (digit - 10));
        }
    }

    char frac_digits[32];
    size_t frac_len = 0;
    if (frac > 0.0) {
        for (int i = 0; i < 20 && frac > 0.0; i++) {
            frac *= (double)radix;
            int digit = (int)floor(frac + 1e-12);
            frac -= digit;
            frac_digits[frac_len++] = (digit < 10) ? (char)('0' + digit) : (char)('a' + (digit - 10));
        }
    }

    size_t total_len = (negative ? 1 : 0) + int_len + (frac_len ? (1 + frac_len) : 0);
    char *out = malloc(total_len);
    if (!out) {
        free(int_digits);
        return ps_string_from_cstr("");
    }
    size_t pos = 0;
    if (negative) out[pos++] = '-';
    for (size_t i = 0; i < int_len; i++) {
        out[pos++] = int_digits[int_len - 1 - i];
    }
    if (frac_len) {
        out[pos++] = '.';
        memcpy(out + pos, frac_digits, frac_len);
        pos += frac_len;
    }
    free(int_digits);
    return ps_string_from_utf8(out, total_len);
}

static PSValue ps_native_number_to_string(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    if (!(this_val.type == PS_T_NUMBER ||
          (this_val.type == PS_T_OBJECT && this_val.as.object &&
           this_val.as.object->kind == PS_OBJ_KIND_NUMBER))) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    int radix = 10;
    if (argc > 0 && argv[0].type != PS_T_UNDEFINED) {
        double r = ps_to_number(vm, argv[0]);
        if (vm && vm->has_pending_throw) return ps_value_undefined();
        if (isnan(r) || isinf(r)) {
            char msg[96];
            snprintf(msg, sizeof(msg), "Invalid radix: %.15g", r);
            ps_vm_throw_range_error(vm, msg);
            return ps_value_undefined();
        }
        int r_int = (r < 0.0) ? (int)ceil(r) : (int)floor(r);
        if (r_int < 2 || r_int > 36) {
            char msg[96];
            snprintf(msg, sizeof(msg), "Invalid radix: %.15g", r);
            ps_vm_throw_range_error(vm, msg);
            return ps_value_undefined();
        }
        radix = r_int;
    }

    double num = ps_to_number(vm, this_val);
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    if (radix == 10) {
        PSValue num_val = ps_value_number(num);
        return ps_value_string(ps_value_to_string(&num_val));
    }
    return ps_value_string(ps_number_to_string_radix(num, radix));
}

static PSValue ps_native_number_to_fixed(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    if (!(this_val.type == PS_T_NUMBER ||
          (this_val.type == PS_T_OBJECT && this_val.as.object &&
           this_val.as.object->kind == PS_OBJ_KIND_NUMBER))) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    double num = ps_to_number(vm, this_val);
    int digits = 0;
    if (argc > 0) {
        double d = ps_to_number(vm, argv[0]);
        if (!isnan(d) && !isinf(d)) {
            if (d < 0.0) d = 0.0;
            if (d > 20.0) d = 20.0;
            digits = (int)d;
        }
    }
    if (isnan(num)) {
        return ps_value_string(ps_string_from_cstr("NaN"));
    }
    if (isinf(num)) {
        return ps_value_string(ps_string_from_cstr(num < 0 ? "-Infinity" : "Infinity"));
    }
    if (num == 0.0) {
        num = 0.0;
    }
    double factor = pow(10.0, (double)digits);
    if (factor != 0.0) {
        num = round(num * factor) / factor;
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "%.*f", digits, num);
    return ps_value_string(ps_string_from_cstr(buf));
}

static PSString *ps_format_exponent(const char *buf, int keep_trailing) {
    const char *e = strchr(buf, 'e');
    if (!e) e = strchr(buf, 'E');
    if (!e) return ps_string_from_cstr(buf);
    size_t mantissa_len = (size_t)(e - buf);
    char mantissa[128];
    if (mantissa_len >= sizeof(mantissa)) mantissa_len = sizeof(mantissa) - 1;
    memcpy(mantissa, buf, mantissa_len);
    mantissa[mantissa_len] = '\0';
    if (!keep_trailing) {
        char *dot = strchr(mantissa, '.');
        if (dot) {
            char *end = mantissa + strlen(mantissa) - 1;
            while (end > dot && *end == '0') {
                *end-- = '\0';
            }
            if (end == dot) *end = '\0';
        }
    }
    int exp_val = 0;
    const char *exp_str = e + 1;
    if (*exp_str == '+') exp_str++;
    exp_val = (int)strtol(exp_str, NULL, 10);
    char out[160];
    snprintf(out, sizeof(out), "%se%+d", mantissa, exp_val);
    return ps_string_from_cstr(out);
}

static PSString *ps_format_precision(double num, int precision) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%.*g", precision, num);
    if (strchr(buf, 'e') || strchr(buf, 'E')) {
        return ps_format_exponent(buf, 1);
    }
    int sig = 0;
    int has_dot = 0;
    for (const char *p = buf; *p; p++) {
        if (*p == '.') {
            has_dot = 1;
            continue;
        }
        if (*p >= '0' && *p <= '9') sig++;
    }
    if (sig < precision) {
        char out[160];
        size_t len = strlen(buf);
        if (!has_dot) {
            snprintf(out, sizeof(out), "%s.", buf);
            len = strlen(out);
            for (int i = sig; i < precision && len + 1 < sizeof(out); i++) {
                out[len++] = '0';
            }
            out[len] = '\0';
            return ps_string_from_cstr(out);
        }
        snprintf(out, sizeof(out), "%s", buf);
        len = strlen(out);
        for (int i = sig; i < precision && len + 1 < sizeof(out); i++) {
            out[len++] = '0';
        }
        out[len] = '\0';
        return ps_string_from_cstr(out);
    }
    return ps_string_from_cstr(buf);
}

static PSValue ps_native_number_to_exponential(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    if (!(this_val.type == PS_T_NUMBER ||
          (this_val.type == PS_T_OBJECT && this_val.as.object &&
           this_val.as.object->kind == PS_OBJ_KIND_NUMBER))) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    double num = ps_to_number(vm, this_val);
    int digits = -1;
    if (argc > 0 && argv[0].type != PS_T_UNDEFINED) {
        double d = ps_to_number(vm, argv[0]);
        if (!isnan(d) && !isinf(d)) {
            if (d < 0.0) d = 0.0;
            if (d > 20.0) d = 20.0;
            digits = (int)d;
        }
    }
    if (isnan(num)) {
        return ps_value_string(ps_string_from_cstr("NaN"));
    }
    if (isinf(num)) {
        return ps_value_string(ps_string_from_cstr(num < 0 ? "-Infinity" : "Infinity"));
    }
    char buf[128];
    if (digits < 0) {
        snprintf(buf, sizeof(buf), "%.15e", num);
        return ps_value_string(ps_format_exponent(buf, 0));
    }
    snprintf(buf, sizeof(buf), "%.*e", digits, num);
    return ps_value_string(ps_format_exponent(buf, 1));
}

static PSValue ps_native_number_to_precision(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    if (!(this_val.type == PS_T_NUMBER ||
          (this_val.type == PS_T_OBJECT && this_val.as.object &&
           this_val.as.object->kind == PS_OBJ_KIND_NUMBER))) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    double num = ps_to_number(vm, this_val);
    if (argc == 0 || argv[0].type == PS_T_UNDEFINED) {
        return ps_value_string(ps_value_to_string(&this_val));
    }
    int precision = 1;
    double d = ps_to_number(vm, argv[0]);
    if (!isnan(d) && !isinf(d)) {
        if (d < 1.0) d = 1.0;
        if (d > 21.0) d = 21.0;
        precision = (int)d;
    }
    if (isnan(num)) {
        return ps_value_string(ps_string_from_cstr("NaN"));
    }
    if (isinf(num)) {
        return ps_value_string(ps_string_from_cstr(num < 0 ? "-Infinity" : "Infinity"));
    }
    return ps_value_string(ps_format_precision(num, precision));
}

static PSValue ps_native_number_value_of(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)argc;
    (void)argv;
    if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    if (this_val.type == PS_T_OBJECT && this_val.as.object &&
        this_val.as.object->kind == PS_OBJ_KIND_NUMBER &&
        this_val.as.object->internal) {
        return *((PSValue *)this_val.as.object->internal);
    }
    if (this_val.type == PS_T_NUMBER) {
        return this_val;
    }
    ps_vm_throw_type_error(vm, "Invalid receiver");
    return ps_value_undefined();
}

static PSValue ps_native_string_to_string(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)argc;
    (void)argv;
    if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    if (this_val.type == PS_T_STRING) {
        return ps_value_string(this_val.as.string);
    }
    if (this_val.type == PS_T_OBJECT && this_val.as.object &&
        this_val.as.object->kind == PS_OBJ_KIND_STRING) {
        return ps_value_string(ps_value_to_string(&this_val));
    }
    ps_vm_throw_type_error(vm, "Invalid receiver");
    return ps_value_undefined();
}

static PSValue ps_native_string_value_of(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)argc;
    (void)argv;
    if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    if (this_val.type == PS_T_OBJECT && this_val.as.object &&
        this_val.as.object->kind == PS_OBJ_KIND_STRING &&
        this_val.as.object->internal) {
        return *((PSValue *)this_val.as.object->internal);
    }
    if (this_val.type == PS_T_STRING) {
        return this_val;
    }
    ps_vm_throw_type_error(vm, "Invalid receiver");
    return ps_value_undefined();
}

static PSValue ps_native_string_char_at(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    PSString *s = ps_to_string(vm, this_val);
    double idx_num = (argc > 0) ? ps_to_number(vm, argv[0]) : 0.0;
    if (isnan(idx_num) || idx_num < 0.0 || isinf(idx_num)) {
        return ps_value_string(ps_string_from_cstr(""));
    }
    size_t idx = (size_t)idx_num;
    if (!s || idx >= s->glyph_count) {
        return ps_value_string(ps_string_from_cstr(""));
    }
    return ps_value_string(ps_string_char_at(s, idx));
}

static PSValue ps_native_string_char_code_at(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    PSString *s = ps_to_string(vm, this_val);
    double idx_num = (argc > 0) ? ps_to_number(vm, argv[0]) : 0.0;
    if (isnan(idx_num) || idx_num < 0.0 || isinf(idx_num)) {
        return ps_value_number(0.0 / 0.0);
    }
    size_t idx = (size_t)idx_num;
    if (!s || idx >= s->glyph_count) {
        return ps_value_number(0.0 / 0.0);
    }
    return ps_value_number((double)ps_string_char_code_at(s, idx));
}

static size_t ps_string_index_of(PSString *s, PSString *needle, size_t start) {
    if (!s || !needle) return (size_t)-1;
    if (needle->glyph_count == 0) {
        return start <= s->glyph_count ? start : s->glyph_count;
    }
    if (start >= s->glyph_count) return (size_t)-1;
    if (needle->glyph_count > s->glyph_count) return (size_t)-1;
    for (size_t i = start; i + needle->glyph_count <= s->glyph_count; i++) {
        int match = 1;
        for (size_t j = 0; j < needle->glyph_count; j++) {
            uint32_t ca = ps_string_char_code_at(s, i + j);
            uint32_t cb = ps_string_char_code_at(needle, j);
            if (ca != cb) {
                match = 0;
                break;
            }
        }
        if (match) return i;
    }
    return (size_t)-1;
}

static PSString *ps_string_substring(const PSString *s, size_t start, size_t end) {
    if (!s || start >= s->glyph_count || start >= end) {
        return ps_string_from_cstr("");
    }
    if (end > s->glyph_count) end = s->glyph_count;
    size_t byte_start = 0;
    size_t byte_end = 0;
    if (!s->glyph_offsets) {
        byte_start = start;
        byte_end = end;
    } else {
        byte_start = s->glyph_offsets[start];
        byte_end = (end < s->glyph_count) ? s->glyph_offsets[end] : s->byte_len;
    }
    return ps_string_from_utf8(s->utf8 + byte_start, byte_end - byte_start);
}

static PSString *ps_string_append(PSString *base, PSString *piece) {
    if (!piece) return base ? base : ps_string_from_cstr("");
    if (!base) return piece;
    return ps_string_concat(base, piece);
}

static PSString *ps_string_replace_build(PSString *tmpl,
                                         PSString *full_match,
                                         PSString **captures,
                                         size_t cap_count) {
    if (!tmpl) return ps_string_from_cstr("");
    PSString *out = NULL;
    size_t last = 0;
    for (size_t i = 0; i < tmpl->byte_len; i++) {
        if (tmpl->utf8[i] != '$' || i + 1 >= tmpl->byte_len) continue;
        if (i > last) {
            PSString *lit = ps_string_from_utf8(tmpl->utf8 + last, i - last);
            out = ps_string_append(out, lit);
        }
        unsigned char next = (unsigned char)tmpl->utf8[i + 1];
        if (next == '$') {
            PSString *lit = ps_string_from_utf8((const char *)&next, 1);
            out = ps_string_append(out, lit);
        } else if (next == '&') {
            out = ps_string_append(out, full_match);
        } else if (next >= '1' && next <= '9') {
            size_t idx = (size_t)(next - '0');
            if (idx < cap_count && captures && captures[idx]) {
                out = ps_string_append(out, captures[idx]);
            }
        } else {
            PSString *lit = ps_string_from_utf8(tmpl->utf8 + i, 2);
            out = ps_string_append(out, lit);
        }
        i++;
        last = i + 1;
    }
    if (last < tmpl->byte_len) {
        PSString *lit = ps_string_from_utf8(tmpl->utf8 + last, tmpl->byte_len - last);
        out = ps_string_append(out, lit);
    }
    return out ? out : ps_string_from_cstr("");
}

static PSValue ps_native_string_index_of(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    PSString *s = ps_to_string(vm, this_val);
    PSString *needle = ps_string_from_cstr("");
    if (argc > 0) {
        needle = ps_to_string(vm, argv[0]);
    }
    size_t start = 0;
    if (argc > 1) {
        double d = ps_to_number(vm, argv[1]);
        if (!isnan(d) && !isinf(d) && d > 0.0) {
            start = (size_t)d;
        }
    }
    size_t idx = ps_string_index_of(s, needle, start);
    if (idx == (size_t)-1) {
        return ps_value_number(-1.0);
    }
    return ps_value_number((double)idx);
}

static PSValue ps_native_string_last_index_of(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    PSString *s = ps_to_string(vm, this_val);
    PSString *needle = ps_string_from_cstr("");
    if (argc > 0) {
        needle = ps_to_string(vm, argv[0]);
    }
    size_t len = s ? s->glyph_count : 0;
    size_t pos = len;
    if (argc > 1) {
        double d = ps_to_number(vm, argv[1]);
        if (!isnan(d) && !isinf(d)) {
            if (d < 0.0) pos = 0;
            else if ((size_t)d < pos) pos = (size_t)d;
        }
    }
    if (!needle || needle->glyph_count == 0) {
        return ps_value_number((double)pos);
    }
    if (!s || needle->glyph_count > len) {
        return ps_value_number(-1.0);
    }
    size_t start = pos;
    if (start + needle->glyph_count > len) {
        if (needle->glyph_count > len) return ps_value_number(-1.0);
        start = len - needle->glyph_count;
    }
    for (size_t i = start + 1; i-- > 0;) {
        int match = 1;
        for (size_t j = 0; j < needle->glyph_count; j++) {
            if (ps_string_char_code_at(s, i + j) != ps_string_char_code_at(needle, j)) {
                match = 0;
                break;
            }
        }
        if (match) return ps_value_number((double)i);
        if (i == 0) break;
    }
    return ps_value_number(-1.0);
}

static PSValue ps_native_string_concat(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    PSString *out = ps_to_string(vm, this_val);
    for (int i = 0; i < argc; i++) {
        PSString *part = ps_to_string(vm, argv[i]);
        out = ps_string_concat(out, part);
    }
    return ps_value_string(out);
}

static PSValue ps_native_string_split(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    PSString *input = ps_to_string(vm, this_val);
    PSObject *out = ps_object_new(vm->array_proto ? vm->array_proto : vm->object_proto);
    if (!out) return ps_value_undefined();
    out->kind = PS_OBJ_KIND_ARRAY;
    size_t out_index = 0;
    if (argc == 0 || argv[0].type == PS_T_UNDEFINED) {
        ps_object_put(out, ps_string_from_cstr("0"), ps_value_string(input));
        ps_object_set_length(out, 1);
        return ps_value_object(out);
    }

    if (argv[0].type == PS_T_OBJECT && argv[0].as.object &&
        argv[0].as.object->kind == PS_OBJ_KIND_REGEXP) {
        PSObject *re = argv[0].as.object;
        int found = 0;
        PSValue ignore_val = ps_object_get(re, ps_string_from_cstr("ignoreCase"), &found);
        int ignore_case = found && ignore_val.type == PS_T_BOOLEAN && ignore_val.as.boolean;
        PSValue pat_val = ps_object_get(re, ps_string_from_cstr("source"), &found);
        PSString *pattern = found && pat_val.type == PS_T_STRING ? pat_val.as.string : ps_string_from_cstr("");
        char flag_buf[4];
        int idx = 0;
        flag_buf[idx++] = 'g';
        if (ignore_case) flag_buf[idx++] = 'i';
        flag_buf[idx] = '\0';
        PSValue args[2];
        args[0] = ps_value_string(pattern);
        args[1] = ps_value_string(ps_string_from_cstr(flag_buf));
        PSValue regex_val = ps_native_regexp(vm, ps_value_undefined(), 2, args);
        if (vm->has_pending_throw) return ps_value_undefined();
        PSObject *regex = regex_val.type == PS_T_OBJECT ? regex_val.as.object : NULL;
        size_t len = input ? input->glyph_count : 0;
        size_t start = 0;
        while (start <= len) {
            ps_object_put(regex, ps_string_from_cstr("lastIndex"), ps_value_number((double)start));
            PSValue exec_args[1];
            exec_args[0] = ps_value_string(input);
            PSValue match = ps_native_regexp_exec(vm, ps_value_object(regex), 1, exec_args);
            if (vm->has_pending_throw || match.type == PS_T_NULL) {
                PSString *tail = ps_string_substring(input, start, len);
                char buf[32];
                snprintf(buf, sizeof(buf), "%zu", out_index++);
                ps_object_put(out, ps_string_from_cstr(buf), ps_value_string(tail));
                break;
            }
            PSObject *match_obj = match.as.object;
            PSValue index_val = ps_object_get(match_obj, ps_string_from_cstr("index"), &found);
            size_t match_start = start;
            if (found) {
                double d = ps_to_number(vm, index_val);
                if (!isnan(d) && d >= 0.0) match_start = (size_t)d;
            }
            PSValue match0_val = ps_object_get(match_obj, ps_string_from_cstr("0"), &found);
            PSString *match0 = found && match0_val.type == PS_T_STRING ? match0_val.as.string : ps_string_from_cstr("");
            size_t match_len = match0 ? match0->glyph_count : 0;
            PSString *chunk = ps_string_substring(input, start, match_start);
            char buf[32];
            snprintf(buf, sizeof(buf), "%zu", out_index++);
            ps_object_put(out, ps_string_from_cstr(buf), ps_value_string(chunk));
            PSValue len_val = ps_object_get(match_obj, ps_string_from_cstr("length"), &found);
            size_t cap_len = 0;
            if (found) {
                double d = ps_to_number(vm, len_val);
                if (!isnan(d) && d > 1.0) cap_len = (size_t)d;
            }
            for (size_t i = 1; i < cap_len; i++) {
                char cap_buf[32];
                snprintf(cap_buf, sizeof(cap_buf), "%zu", i);
                PSValue cap_val = ps_object_get(match_obj, ps_string_from_cstr(cap_buf), &found);
                if (!found) continue;
                snprintf(buf, sizeof(buf), "%zu", out_index++);
                ps_object_put(out, ps_string_from_cstr(buf), cap_val);
            }
            if (match_len == 0) {
                if (match_start < len) start = match_start + 1;
                else break;
            } else {
                start = match_start + match_len;
            }
        }
        ps_object_set_length(out, out_index);
        ps_object_put(regex, ps_string_from_cstr("lastIndex"), ps_value_number(0.0));
        return ps_value_object(out);
    }

    PSString *sep = ps_to_string(vm, argv[0]);
    if (!sep || sep->glyph_count == 0) {
        size_t len = input ? input->glyph_count : 0;
        for (size_t i = 0; i < len; i++) {
            PSString *ch = ps_string_char_at(input, i);
            char buf[32];
            snprintf(buf, sizeof(buf), "%zu", out_index++);
            ps_object_put(out, ps_string_from_cstr(buf), ps_value_string(ch));
        }
        ps_object_set_length(out, out_index);
        return ps_value_object(out);
    }

    size_t start = 0;
    size_t len = input ? input->glyph_count : 0;
    while (start <= len) {
        size_t idx = ps_string_index_of(input, sep, start);
        if (idx == (size_t)-1) {
            PSString *tail = ps_string_substring(input, start, len);
            char buf[32];
            snprintf(buf, sizeof(buf), "%zu", out_index++);
            ps_object_put(out, ps_string_from_cstr(buf), ps_value_string(tail));
            break;
        }
        PSString *chunk = ps_string_substring(input, start, idx);
        char buf[32];
        snprintf(buf, sizeof(buf), "%zu", out_index++);
        ps_object_put(out, ps_string_from_cstr(buf), ps_value_string(chunk));
        start = idx + sep->glyph_count;
    }
    ps_object_set_length(out, out_index);
    return ps_value_object(out);
}

static PSValue ps_native_string_match(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    PSString *input = ps_to_string(vm, this_val);
    PSValue regex_val;
    if (argc > 0 && argv[0].type == PS_T_OBJECT && argv[0].as.object &&
        argv[0].as.object->kind == PS_OBJ_KIND_REGEXP) {
        regex_val = argv[0];
    } else {
        PSValue args[1];
        args[0] = (argc > 0) ? argv[0] : ps_value_undefined();
        regex_val = ps_native_regexp(vm, ps_value_undefined(), 1, args);
        if (vm->has_pending_throw) return ps_value_undefined();
    }
    PSObject *re = regex_val.as.object;
    int found = 0;
    PSValue global_val = ps_object_get(re, ps_string_from_cstr("global"), &found);
    int global = found && global_val.type == PS_T_BOOLEAN && global_val.as.boolean;
    if (!global) {
        PSValue args[1];
        args[0] = ps_value_string(input);
        return ps_native_regexp_exec(vm, ps_value_object(re), 1, args);
    }
    PSObject *out = ps_object_new(vm->array_proto ? vm->array_proto : vm->object_proto);
    if (!out) return ps_value_undefined();
    out->kind = PS_OBJ_KIND_ARRAY;
    size_t out_index = 0;
    ps_object_put(re, ps_string_from_cstr("lastIndex"), ps_value_number(0.0));
    for (;;) {
        PSValue args[1];
        args[0] = ps_value_string(input);
        PSValue match = ps_native_regexp_exec(vm, ps_value_object(re), 1, args);
        if (vm->has_pending_throw || match.type == PS_T_NULL) break;
        int found_match = 0;
        PSValue match0 = ps_object_get(match.as.object, ps_string_from_cstr("0"), &found_match);
        if (!found_match) break;
        char buf[32];
        snprintf(buf, sizeof(buf), "%zu", out_index++);
        ps_object_put(out, ps_string_from_cstr(buf), match0);
        if (match0.type == PS_T_STRING && match0.as.string &&
            match0.as.string->glyph_count == 0) {
            PSValue idx_val = ps_object_get(re, ps_string_from_cstr("lastIndex"), &found);
            if (found) {
                double d = ps_to_number(vm, idx_val);
                if (!isnan(d)) {
                    size_t next = (size_t)d;
                    if (next < input->glyph_count) {
                        ps_object_put(re, ps_string_from_cstr("lastIndex"),
                                      ps_value_number((double)(next + 1)));
                    }
                }
            }
        }
    }
    ps_object_set_length(out, out_index);
    ps_object_put(re, ps_string_from_cstr("lastIndex"), ps_value_number(0.0));
    if (out_index == 0) return ps_value_null();
    return ps_value_object(out);
}

static PSValue ps_native_string_search(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    PSString *input = ps_to_string(vm, this_val);
    PSValue regex_val;
    if (argc > 0 && argv[0].type == PS_T_OBJECT && argv[0].as.object &&
        argv[0].as.object->kind == PS_OBJ_KIND_REGEXP) {
        regex_val = argv[0];
    } else {
        PSValue args[1];
        args[0] = (argc > 0) ? argv[0] : ps_value_undefined();
        regex_val = ps_native_regexp(vm, ps_value_undefined(), 1, args);
        if (vm->has_pending_throw) return ps_value_undefined();
    }
    PSObject *re = regex_val.as.object;
    if (!re) return ps_value_number(-1.0);
    ps_object_put(re, ps_string_from_cstr("lastIndex"), ps_value_number(0.0));
    PSValue exec_args[1];
    exec_args[0] = ps_value_string(input);
    PSValue match = ps_native_regexp_exec(vm, ps_value_object(re), 1, exec_args);
    if (vm->has_pending_throw) return ps_value_undefined();
    ps_object_put(re, ps_string_from_cstr("lastIndex"), ps_value_number(0.0));
    if (match.type == PS_T_NULL) return ps_value_number(-1.0);
    int found = 0;
    PSValue index_val = ps_object_get(match.as.object, ps_string_from_cstr("index"), &found);
    if (found) {
        double d = ps_to_number(vm, index_val);
        if (!isnan(d)) return ps_value_number(d);
    }
    return ps_value_number(-1.0);
}

static PSValue ps_native_string_replace(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    PSString *input = ps_to_string(vm, this_val);
    PSString *replacement = ps_string_from_cstr("");
    if (argc > 1) replacement = ps_to_string(vm, argv[1]);
    if (argc == 0 || argv[0].type == PS_T_UNDEFINED) {
        return ps_value_string(input);
    }
    if (argv[0].type == PS_T_OBJECT && argv[0].as.object &&
        argv[0].as.object->kind == PS_OBJ_KIND_REGEXP) {
        PSObject *re = argv[0].as.object;
        int found = 0;
        PSValue global_val = ps_object_get(re, ps_string_from_cstr("global"), &found);
        int global = found && global_val.type == PS_T_BOOLEAN && global_val.as.boolean;
        size_t len = input ? input->glyph_count : 0;
        size_t start = 0;
        PSString *out = ps_string_from_cstr("");
        for (;;) {
            ps_object_put(re, ps_string_from_cstr("lastIndex"), ps_value_number((double)start));
            PSValue exec_args[1];
            exec_args[0] = ps_value_string(input);
            PSValue match = ps_native_regexp_exec(vm, ps_value_object(re), 1, exec_args);
            if (vm->has_pending_throw || match.type == PS_T_NULL) {
                PSString *tail = ps_string_substring(input, start, len);
                out = ps_string_concat(out, tail);
                break;
            }
            PSObject *match_obj = match.as.object;
            PSValue index_val = ps_object_get(match_obj, ps_string_from_cstr("index"), &found);
            size_t match_start = start;
            if (found) {
                double d = ps_to_number(vm, index_val);
                if (!isnan(d) && d >= 0.0) match_start = (size_t)d;
            }
            PSValue match0_val = ps_object_get(match_obj, ps_string_from_cstr("0"), &found);
            PSString *match0 = found && match0_val.type == PS_T_STRING ? match0_val.as.string : ps_string_from_cstr("");
            size_t match_len = match0 ? match0->glyph_count : 0;
            PSString *prefix = ps_string_substring(input, start, match_start);
            out = ps_string_concat(out, prefix);
            PSValue len_val = ps_object_get(match_obj, ps_string_from_cstr("length"), &found);
            size_t cap_len = 0;
            if (found) {
                double d = ps_to_number(vm, len_val);
                if (!isnan(d) && d > 0.0) cap_len = (size_t)d;
            }
            PSString **caps = NULL;
            if (cap_len > 0) {
                caps = (PSString **)calloc(cap_len, sizeof(PSString *));
                for (size_t i = 0; i < cap_len; i++) {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%zu", i);
                    PSValue cap_val = ps_object_get(match_obj, ps_string_from_cstr(buf), &found);
                    if (found && cap_val.type == PS_T_STRING) {
                        caps[i] = cap_val.as.string;
                    }
                }
            }
            PSString *rep = ps_string_replace_build(replacement, match0, caps, cap_len);
            out = ps_string_concat(out, rep);
            free(caps);
            if (!global) {
                PSString *tail = ps_string_substring(input, match_start + match_len, len);
                out = ps_string_concat(out, tail);
                break;
            }
            if (match_len == 0) {
                if (match_start < len) start = match_start + 1;
                else break;
            } else {
                start = match_start + match_len;
            }
        }
        ps_object_put(re, ps_string_from_cstr("lastIndex"), ps_value_number(0.0));
        return ps_value_string(out);
    }
    PSString *needle = ps_to_string(vm, argv[0]);
    size_t idx = ps_string_index_of(input, needle, 0);
    if (idx == (size_t)-1) {
        return ps_value_string(input);
    }
    PSString *prefix = ps_string_substring(input, 0, idx);
    PSString *suffix = ps_string_substring(input, idx + needle->glyph_count, input->glyph_count);
    PSString *out = ps_string_concat(prefix, replacement);
    out = ps_string_concat(out, suffix);
    return ps_value_string(out);
}
static PSValue ps_native_string_substring(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    PSString *s = ps_to_string(vm, this_val);
    double start_num = (argc > 0) ? ps_to_number(vm, argv[0]) : 0.0;
    double end_num = (argc > 1) ? ps_to_number(vm, argv[1]) : (double)s->glyph_count;
    if (isnan(start_num) || start_num < 0.0) start_num = 0.0;
    if (isnan(end_num) || end_num < 0.0) end_num = 0.0;
    size_t start = (size_t)start_num;
    size_t end = (size_t)end_num;
    if (start > end) {
        size_t tmp = start;
        start = end;
        end = tmp;
    }
    return ps_value_string(ps_string_substring(s, start, end));
}

static PSValue ps_native_string_slice(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    PSString *s = ps_to_string(vm, this_val);
    double start_num = (argc > 0) ? ps_to_number(vm, argv[0]) : 0.0;
    double end_num = (argc > 1) ? ps_to_number(vm, argv[1]) : (double)s->glyph_count;
    if (isnan(start_num) || isinf(start_num)) start_num = 0.0;
    if (isnan(end_num) || isinf(end_num)) end_num = (double)s->glyph_count;
    if (start_num < 0.0) start_num = (double)s->glyph_count + start_num;
    if (end_num < 0.0) end_num = (double)s->glyph_count + end_num;
    if (start_num < 0.0) start_num = 0.0;
    if (end_num < 0.0) end_num = 0.0;
    if (start_num > (double)s->glyph_count) start_num = (double)s->glyph_count;
    if (end_num > (double)s->glyph_count) end_num = (double)s->glyph_count;
    size_t start = (size_t)start_num;
    size_t end = (size_t)end_num;
    if (end < start) end = start;
    return ps_value_string(ps_string_substring(s, start, end));
}

static size_t ps_object_length(PSObject *obj) {
    if (!obj) return 0;
    if (obj->kind == PS_OBJ_KIND_ARRAY) {
        PSArray *arr = ps_array_from_object(obj);
        if (arr) return arr->length;
    }
    int found = 0;
    PSValue len_val = ps_object_get(obj, ps_string_from_cstr("length"), &found);
    if (!found) return 0;
    double num = ps_to_number(NULL, len_val);
    if (isnan(num) || num < 0.0) return 0;
    return (size_t)num;
}

static int ps_object_has_length(PSObject *obj, size_t *out_len) {
    if (!obj) return 0;
    if (obj->kind == PS_OBJ_KIND_ARRAY) {
        PSArray *arr = ps_array_from_object(obj);
        if (arr) {
            if (out_len) *out_len = arr->length;
            return 1;
        }
    }
    int found = 0;
    PSValue len_val = ps_object_get(obj, ps_string_from_cstr("length"), &found);
    if (!found) return 0;
    double num = ps_to_number(NULL, len_val);
    if (isnan(num) || num < 0.0) return 0;
    if (out_len) *out_len = (size_t)num;
    return 1;
}

static void ps_object_set_length(PSObject *obj, size_t len) {
    if (!obj) return;
    if (obj->kind == PS_OBJ_KIND_ARRAY) {
        (void)ps_array_set_length_internal(obj, len);
        return;
    }
    int found = 0;
    (void)ps_object_get_own(obj, ps_string_from_cstr("length"), &found);
    if (found) {
        ps_object_put(obj,
                      ps_string_from_cstr("length"),
                      ps_value_number((double)len));
        return;
    }
    ps_object_define(obj,
                     ps_string_from_cstr("length"),
                     ps_value_number((double)len),
                     PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
}

static int ps_array_get_index_value(PSVM *vm, PSObject *obj, size_t index, PSValue *out) {
    if (!obj) return 0;
    if (obj->kind == PS_OBJ_KIND_ARRAY) {
        if (ps_array_get_index(obj, index, out)) return 1;
        int found = 0;
        PSValue val = ps_object_get(obj, ps_array_index_string(vm, index), &found);
        if (found && out) *out = val;
        return found;
    }
    int found = 0;
    PSValue val = ps_object_get(obj, ps_array_index_string(vm, index), &found);
    if (found && out) *out = val;
    return found;
}

static void ps_array_set_index_value(PSVM *vm, PSObject *obj, size_t index, PSValue value) {
    if (!obj) return;
    if (obj->kind == PS_OBJ_KIND_ARRAY) {
        (void)ps_array_set_index(obj, index, value);
        return;
    }
    ps_object_put(obj, ps_array_index_string(vm, index), value);
}

static void ps_array_delete_index_value(PSVM *vm, PSObject *obj, size_t index) {
    if (!obj) return;
    if (obj->kind == PS_OBJ_KIND_ARRAY) {
        if (ps_array_delete_index(obj, index)) return;
    }
    int deleted = 0;
    (void)ps_object_delete(obj, ps_array_index_string(vm, index), &deleted);
}

static PSValue ps_native_array_join(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    if (this_val.type != PS_T_OBJECT || !this_val.as.object) {
        return ps_value_string(ps_string_from_cstr(""));
    }
    PSString *sep = ps_string_from_cstr(",");
    if (argc > 0) {
        sep = ps_to_string(vm, argv[0]);
    }
    size_t len = ps_object_length(this_val.as.object);
    if (len == 0) {
        return ps_value_string(ps_string_from_cstr(""));
    }

    PSString **elems = (PSString **)calloc(len, sizeof(PSString *));
    if (!elems) {
        return ps_value_string(ps_string_from_cstr(""));
    }

    size_t total_len = (len > 0) ? (sep->byte_len * (len - 1)) : 0;
    for (size_t i = 0; i < len; i++) {
        PSValue elem = ps_value_undefined();
        int found = ps_array_get_index_value(vm, this_val.as.object, i, &elem);
        if (!found || elem.type == PS_T_UNDEFINED || elem.type == PS_T_NULL) {
            elems[i] = NULL;
            continue;
        }
        PSString *elem_str = ps_to_string(vm, elem);
        elems[i] = elem_str;
        if (elem_str) {
            total_len += elem_str->byte_len;
        }
    }

    if (total_len == 0) {
        free(elems);
        return ps_value_string(ps_string_from_cstr(""));
    }

    char *buf = (char *)malloc(total_len);
    if (!buf) {
        free(elems);
        return ps_value_string(ps_string_from_cstr(""));
    }

    char *p = buf;
    for (size_t i = 0; i < len; i++) {
        if (i > 0 && sep && sep->byte_len > 0) {
            memcpy(p, sep->utf8, sep->byte_len);
            p += sep->byte_len;
        }
        PSString *elem_str = elems[i];
        if (elem_str && elem_str->byte_len > 0) {
            memcpy(p, elem_str->utf8, elem_str->byte_len);
            p += elem_str->byte_len;
        }
    }

    PSString *result = ps_string_from_utf8(buf, total_len);
    free(buf);
    free(elems);
    if (!result) {
        return ps_value_string(ps_string_from_cstr(""));
    }
    return ps_value_string(result);
}

static PSValue ps_native_array_to_string(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)argc;
    (void)argv;
    return ps_native_array_join(vm, this_val, 0, NULL);
}

static PSValue ps_native_array_push(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    if (this_val.type != PS_T_OBJECT || !this_val.as.object) {
        return ps_value_undefined();
    }
    PSObject *obj = this_val.as.object;
    size_t len = ps_object_length(obj);
    for (int i = 0; i < argc; i++) {
        ps_array_set_index_value(vm, obj, len + (size_t)i, argv[i]);
    }
    len += (size_t)argc;
    ps_object_set_length(obj, len);
    return ps_value_number((double)len);
}

static PSValue ps_native_array_pop(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)argc;
    (void)argv;
    if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    if (this_val.type != PS_T_OBJECT || !this_val.as.object) {
        return ps_value_undefined();
    }
    PSObject *obj = this_val.as.object;
    size_t len = ps_object_length(obj);
    if (len == 0) {
        return ps_value_undefined();
    }
    size_t idx = len - 1;
    PSValue elem = ps_value_undefined();
    int found = ps_array_get_index_value(vm, obj, idx, &elem);
    ps_array_delete_index_value(vm, obj, idx);
    ps_object_set_length(obj, idx);
    return found ? elem : ps_value_undefined();
}

static PSValue ps_native_array_shift(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)argc;
    (void)argv;
    if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    if (this_val.type != PS_T_OBJECT || !this_val.as.object) {
        return ps_value_undefined();
    }
    PSObject *obj = this_val.as.object;
    size_t len = ps_object_length(obj);
    if (len == 0) {
        return ps_value_undefined();
    }
    PSValue first = ps_value_undefined();
    int found = ps_array_get_index_value(vm, obj, 0, &first);
    for (size_t i = 1; i < len; i++) {
        PSValue val = ps_value_undefined();
        int got = ps_array_get_index_value(vm, obj, i, &val);
        if (got) {
            ps_array_set_index_value(vm, obj, i - 1, val);
        } else {
            ps_array_delete_index_value(vm, obj, i - 1);
        }
    }
    ps_array_delete_index_value(vm, obj, len - 1);
    ps_object_set_length(obj, len - 1);
    return found ? first : ps_value_undefined();
}

static PSValue ps_native_array_unshift(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    if (this_val.type != PS_T_OBJECT || !this_val.as.object) {
        return ps_value_undefined();
    }
    PSObject *obj = this_val.as.object;
    size_t len = ps_object_length(obj);
    for (size_t i = len; i > 0; i--) {
        PSValue val = ps_value_undefined();
        int got = ps_array_get_index_value(vm, obj, i - 1, &val);
        if (got) {
            ps_array_set_index_value(vm, obj, i - 1 + (size_t)argc, val);
        } else {
            ps_array_delete_index_value(vm, obj, i - 1 + (size_t)argc);
        }
    }
    for (int i = 0; i < argc; i++) {
        ps_array_set_index_value(vm, obj, (size_t)i, argv[i]);
    }
    size_t new_len = len + (size_t)argc;
    ps_object_set_length(obj, new_len);
    return ps_value_number((double)new_len);
}

static PSValue ps_native_array_slice(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    if (this_val.type != PS_T_OBJECT || !this_val.as.object) {
        return ps_value_undefined();
    }
    PSObject *obj = this_val.as.object;
    size_t len = ps_object_length(obj);
    double start_num = (argc > 0) ? ps_to_number(vm, argv[0]) : 0.0;
    double end_num = (argc > 1) ? ps_to_number(vm, argv[1]) : (double)len;
    if (isnan(start_num)) start_num = 0.0;
    if (isnan(end_num)) end_num = (double)len;
    if (start_num < 0.0) start_num = (double)len + start_num;
    if (end_num < 0.0) end_num = (double)len + end_num;
    if (start_num < 0.0) start_num = 0.0;
    if (end_num < 0.0) end_num = 0.0;
    if (start_num > (double)len) start_num = (double)len;
    if (end_num > (double)len) end_num = (double)len;
    size_t start = (size_t)start_num;
    size_t end = (size_t)end_num;
    if (end < start) end = start;

    PSObject *out = ps_object_new(vm->array_proto ? vm->array_proto : vm->object_proto);
    if (!out) return ps_value_undefined();
    out->kind = PS_OBJ_KIND_ARRAY;
    (void)ps_array_init(out);
    size_t out_index = 0;
    for (size_t i = start; i < end; i++) {
        PSValue val = ps_value_undefined();
        int got = ps_array_get_index_value(vm, obj, i, &val);
        if (got) {
            (void)ps_array_set_index(out, out_index++, val);
        }
    }
    ps_object_set_length(out, out_index);
    return ps_value_object(out);
}

static PSValue ps_native_array_concat(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    if (this_val.type != PS_T_OBJECT || !this_val.as.object) {
        return ps_value_undefined();
    }
    PSObject *out = ps_object_new(vm->array_proto ? vm->array_proto : vm->object_proto);
    if (!out) return ps_value_undefined();
    out->kind = PS_OBJ_KIND_ARRAY;
    (void)ps_array_init(out);

    size_t out_index = 0;
    size_t len = ps_object_length(this_val.as.object);
    for (size_t i = 0; i < len; i++) {
        PSValue val = ps_value_undefined();
        int got = ps_array_get_index_value(vm, this_val.as.object, i, &val);
        if (got) {
            (void)ps_array_set_index(out, out_index, val);
        }
        out_index++;
    }

    for (int i = 0; i < argc; i++) {
        if (argv[i].type == PS_T_OBJECT && argv[i].as.object) {
            size_t alen = 0;
            if (ps_object_has_length(argv[i].as.object, &alen)) {
                for (size_t j = 0; j < alen; j++) {
                    PSValue val = ps_value_undefined();
                    int got = ps_array_get_index_value(vm, argv[i].as.object, j, &val);
                    if (got) {
                        (void)ps_array_set_index(out, out_index, val);
                    }
                    out_index++;
                }
                continue;
            }
        }
        (void)ps_array_set_index(out, out_index++, argv[i]);
    }

    ps_object_set_length(out, out_index);
    return ps_value_object(out);
}

static PSValue ps_native_array_reverse(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)argc;
    (void)argv;
    if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    if (this_val.type != PS_T_OBJECT || !this_val.as.object) {
        return ps_value_undefined();
    }
    PSObject *obj = this_val.as.object;
    size_t len = ps_object_length(obj);
    for (size_t i = 0; i < len / 2; i++) {
        size_t j = len - 1 - i;
        PSValue a_val = ps_value_undefined();
        PSValue b_val = ps_value_undefined();
        int got_a = ps_array_get_index_value(vm, obj, i, &a_val);
        int got_b = ps_array_get_index_value(vm, obj, j, &b_val);
        if (got_a) {
            ps_array_set_index_value(vm, obj, j, a_val);
        } else {
            ps_array_delete_index_value(vm, obj, j);
        }
        if (got_b) {
            ps_array_set_index_value(vm, obj, i, b_val);
        } else {
            ps_array_delete_index_value(vm, obj, i);
        }
    }
    return this_val;
}

static int ps_array_sort_compare(PSVM *vm, const PSValue *a, const PSValue *b) {
    PSString *sa = ps_to_string(vm, *a);
    PSString *sb = ps_to_string(vm, *b);
    size_t min = (sa->glyph_count < sb->glyph_count) ? sa->glyph_count : sb->glyph_count;
    for (size_t i = 0; i < min; i++) {
        uint32_t ca = ps_string_char_code_at(sa, i);
        uint32_t cb = ps_string_char_code_at(sb, i);
        if (ca < cb) return -1;
        if (ca > cb) return 1;
    }
    if (sa->glyph_count < sb->glyph_count) return -1;
    if (sa->glyph_count > sb->glyph_count) return 1;
    return 0;
}

static PSValue ps_native_array_sort(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)argc;
    (void)argv;
    if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    if (this_val.type != PS_T_OBJECT || !this_val.as.object) {
        return ps_value_undefined();
    }
    PSObject *obj = this_val.as.object;
    size_t len = ps_object_length(obj);
    PSValue *items = NULL;
    size_t count = 0;
    if (len > 0) {
        items = (PSValue *)calloc(len, sizeof(PSValue));
        if (!items) return this_val;
    }
    for (size_t i = 0; i < len; i++) {
        PSValue val = ps_value_undefined();
        int got = ps_array_get_index_value(vm, obj, i, &val);
        if (got) {
            items[count++] = val;
        }
    }
    for (size_t i = 0; i < count; i++) {
        for (size_t j = i + 1; j < count; j++) {
            if (ps_array_sort_compare(vm, &items[j], &items[i]) < 0) {
                PSValue tmp = items[i];
                items[i] = items[j];
                items[j] = tmp;
            }
        }
    }
    for (size_t i = 0; i < len; i++) {
        if (i < count) {
            ps_array_set_index_value(vm, obj, i, items[i]);
        } else {
            ps_array_delete_index_value(vm, obj, i);
        }
    }
    free(items);
    return this_val;
}

static PSValue ps_native_array_splice(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    if (this_val.type == PS_T_NULL || this_val.type == PS_T_UNDEFINED) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    if (this_val.type != PS_T_OBJECT || !this_val.as.object) {
        return ps_value_undefined();
    }
    PSObject *obj = this_val.as.object;
    size_t len = ps_object_length(obj);
    double start_num = (argc > 0) ? ps_to_number(vm, argv[0]) : 0.0;
    if (isnan(start_num)) start_num = 0.0;
    if (start_num < 0.0) start_num = (double)len + start_num;
    if (start_num < 0.0) start_num = 0.0;
    if (start_num > (double)len) start_num = (double)len;
    size_t start = (size_t)start_num;
    size_t delete_count = 0;
    if (argc < 2) {
        delete_count = len - start;
    } else {
        double del_num = ps_to_number(vm, argv[1]);
        if (isnan(del_num) || del_num < 0.0) del_num = 0.0;
        if (del_num > (double)(len - start)) del_num = (double)(len - start);
        delete_count = (size_t)del_num;
    }
    size_t insert_count = (argc > 2) ? (size_t)(argc - 2) : 0;

    PSObject *out = ps_object_new(vm->array_proto ? vm->array_proto : vm->object_proto);
    if (!out) return ps_value_undefined();
    out->kind = PS_OBJ_KIND_ARRAY;
    (void)ps_array_init(out);
    for (size_t i = 0; i < delete_count; i++) {
        PSValue val = ps_value_undefined();
        int got = ps_array_get_index_value(vm, obj, start + i, &val);
        if (got) {
            (void)ps_array_set_index(out, i, val);
        }
    }
    ps_object_set_length(out, delete_count);

    if (insert_count < delete_count) {
        size_t shift = delete_count - insert_count;
        for (size_t i = start + delete_count; i < len; i++) {
            PSValue val = ps_value_undefined();
            int got = ps_array_get_index_value(vm, obj, i, &val);
            if (got) {
                ps_array_set_index_value(vm, obj, i - shift, val);
            } else {
                ps_array_delete_index_value(vm, obj, i - shift);
            }
        }
        for (size_t i = len; i > len - shift; i--) {
            ps_array_delete_index_value(vm, obj, i - 1);
        }
    } else if (insert_count > delete_count) {
        size_t shift = insert_count - delete_count;
        for (size_t i = len; i > start + delete_count; i--) {
            PSValue val = ps_value_undefined();
            int got = ps_array_get_index_value(vm, obj, i - 1, &val);
            if (got) {
                ps_array_set_index_value(vm, obj, i - 1 + shift, val);
            } else {
                ps_array_delete_index_value(vm, obj, i - 1 + shift);
            }
        }
    }
    for (size_t i = 0; i < insert_count; i++) {
        ps_array_set_index_value(vm, obj, start + i, argv[2 + (int)i]);
    }
    size_t new_len = len - delete_count + insert_count;
    ps_object_set_length(obj, new_len);
    return ps_value_object(out);
}

static PSValue ps_native_math_abs(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    double x = (argc > 0) ? ps_to_number(vm, argv[0]) : 0.0;
    return ps_value_number(fabs(x));
}

static PSValue ps_native_math_floor(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    double x = (argc > 0) ? ps_to_number(vm, argv[0]) : 0.0;
    return ps_value_number(floor(x));
}

static PSValue ps_native_math_ceil(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    double x = (argc > 0) ? ps_to_number(vm, argv[0]) : 0.0;
    return ps_value_number(ceil(x));
}

static PSValue ps_native_math_max(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    if (argc == 0) return ps_value_number(-INFINITY);
    double max = -INFINITY;
    for (int i = 0; i < argc; i++) {
        double v = ps_to_number(vm, argv[i]);
        if (isnan(v)) return ps_value_number(0.0 / 0.0);
        if (v > max) max = v;
    }
    return ps_value_number(max);
}

static PSValue ps_native_math_min(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    if (argc == 0) return ps_value_number(INFINITY);
    double min = INFINITY;
    for (int i = 0; i < argc; i++) {
        double v = ps_to_number(vm, argv[i]);
        if (isnan(v)) return ps_value_number(0.0 / 0.0);
        if (v < min) min = v;
    }
    return ps_value_number(min);
}

static PSValue ps_native_math_pow(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    double x = (argc > 0) ? ps_to_number(vm, argv[0]) : 0.0;
    double y = (argc > 1) ? ps_to_number(vm, argv[1]) : 0.0;
    return ps_value_number(pow(x, y));
}

static PSValue ps_native_math_sqrt(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    double x = (argc > 0) ? ps_to_number(vm, argv[0]) : 0.0;
    return ps_value_number(sqrt(x));
}

static PSValue ps_native_math_random(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    (void)argc;
    (void)argv;
    double v = (double)rand() / ((double)RAND_MAX + 1.0);
    return ps_value_number(v);
}

static PSValue ps_native_math_round(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    double x = (argc > 0) ? ps_to_number(vm, argv[0]) : 0.0;
    if (isnan(x) || isinf(x)) return ps_value_number(x);
    double r = floor(x + 0.5);
    if (r == 0.0 && x < 0.0) r = -0.0;
    return ps_value_number(r);
}

static PSValue ps_native_math_sin(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    double x = (argc > 0) ? ps_to_number(vm, argv[0]) : 0.0;
    return ps_value_number(sin(x));
}

static PSValue ps_native_math_cos(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    double x = (argc > 0) ? ps_to_number(vm, argv[0]) : 0.0;
    return ps_value_number(cos(x));
}

static PSValue ps_native_math_tan(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    double x = (argc > 0) ? ps_to_number(vm, argv[0]) : 0.0;
    return ps_value_number(tan(x));
}

static PSValue ps_native_math_asin(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    double x = (argc > 0) ? ps_to_number(vm, argv[0]) : 0.0;
    return ps_value_number(asin(x));
}

static PSValue ps_native_math_acos(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    double x = (argc > 0) ? ps_to_number(vm, argv[0]) : 0.0;
    return ps_value_number(acos(x));
}

static PSValue ps_native_math_atan(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    double x = (argc > 0) ? ps_to_number(vm, argv[0]) : 0.0;
    return ps_value_number(atan(x));
}

static PSValue ps_native_math_atan2(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    double y = (argc > 0) ? ps_to_number(vm, argv[0]) : 0.0 / 0.0;
    double x = (argc > 1) ? ps_to_number(vm, argv[1]) : 0.0 / 0.0;
    return ps_value_number(atan2(y, x));
}

static PSValue ps_native_math_exp(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    double x = (argc > 0) ? ps_to_number(vm, argv[0]) : 0.0;
    return ps_value_number(exp(x));
}

static PSValue ps_native_math_log(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    double x = (argc > 0) ? ps_to_number(vm, argv[0]) : 0.0;
    return ps_value_number(log(x));
}

static PSValue ps_native_date_to_string(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)argc;
    (void)argv;
    if (this_val.type == PS_T_OBJECT && this_val.as.object &&
        this_val.as.object->kind == PS_OBJ_KIND_DATE) {
        PSValue stored = ps_value_number(0.0);
        if (this_val.as.object->internal) {
            stored = *((PSValue *)this_val.as.object->internal);
        }
        if (stored.type != PS_T_NUMBER) {
            return ps_value_string(ps_string_from_cstr("Invalid Date"));
        }
        return ps_value_string(ps_date_format_utc(stored.as.number));
    }
    return ps_value_string(ps_string_from_cstr("[object Date]"));
}

static PSValue ps_native_date_get_time(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)argc;
    (void)argv;
    if (this_val.type != PS_T_OBJECT || !this_val.as.object ||
        this_val.as.object->kind != PS_OBJ_KIND_DATE) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    if (this_val.as.object->internal) {
        return *((PSValue *)this_val.as.object->internal);
    }
    return ps_value_number(0.0);
}

static int ps_date_get_ms_value(PSVM *vm, PSValue this_val, double *out_ms) {
    if (this_val.type != PS_T_OBJECT || !this_val.as.object ||
        this_val.as.object->kind != PS_OBJ_KIND_DATE) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return 0;
    }
    if (this_val.as.object->internal) {
        PSValue stored = *((PSValue *)this_val.as.object->internal);
        if (stored.type == PS_T_NUMBER) {
            if (out_ms) *out_ms = stored.as.number;
            return 1;
        }
        if (out_ms) *out_ms = 0.0 / 0.0;
        return 1;
    }
    if (out_ms) *out_ms = 0.0;
    return 1;
}

static int ps_date_to_local_tm(double ms_num, struct tm *out_tm, int *out_ms) {
    if (isnan(ms_num) || isinf(ms_num)) return 0;
    int64_t ms = (int64_t)floor(ms_num);
    int64_t sec = ms / 1000LL;
    int ms_part = (int)(ms % 1000LL);
    if (ms_part < 0) {
        ms_part += 1000;
        sec -= 1;
    }
    time_t t = (time_t)sec;
    struct tm *tmv = localtime(&t);
    if (!tmv) return 0;
    if (out_tm) *out_tm = *tmv;
    if (out_ms) *out_ms = ms_part;
    return 1;
}

static int ps_date_timezone_offset_minutes(time_t t) {
    struct tm local_tm = *localtime(&t);
    struct tm gm_tm = *gmtime(&t);
    time_t local_sec = mktime(&local_tm);
    time_t gm_sec = mktime(&gm_tm);
    double diff = difftime(gm_sec, local_sec);
    return (int)(diff / 60.0);
}

static PSString *ps_date_format_local(double ms_num) {
    if (isnan(ms_num) || isinf(ms_num)) {
        return ps_string_from_cstr("Invalid Date");
    }
    struct tm tmv;
    if (!ps_date_to_local_tm(ms_num, &tmv, NULL)) {
        return ps_string_from_cstr("Invalid Date");
    }
    const char *weekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    time_t t = (time_t)floor(ms_num / 1000.0);
    int offset = -ps_date_timezone_offset_minutes(t);
    int off_h = offset / 60;
    int off_m = abs(offset % 60);
    char buf[64];
    snprintf(buf, sizeof(buf), "%s %s %02d %04d %02d:%02d:%02d GMT%+03d%02d",
             weekdays[tmv.tm_wday], months[tmv.tm_mon], tmv.tm_mday,
             tmv.tm_year + 1900, tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
             off_h, off_m);
    return ps_string_from_cstr(buf);
}

static PSValue ps_native_date_to_utc_string(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)argc;
    (void)argv;
    if (this_val.type == PS_T_OBJECT && this_val.as.object &&
        this_val.as.object->kind == PS_OBJ_KIND_DATE) {
        double ms = 0.0;
        if (this_val.as.object->internal) {
            PSValue stored = *((PSValue *)this_val.as.object->internal);
            if (stored.type == PS_T_NUMBER) ms = stored.as.number;
            else ms = 0.0 / 0.0;
        }
        return ps_value_string(ps_date_format_utc(ms));
    }
    return ps_value_string(ps_string_from_cstr("[object Date]"));
}

static PSValue ps_native_date_to_locale_string(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)argc;
    (void)argv;
    if (this_val.type == PS_T_OBJECT && this_val.as.object &&
        this_val.as.object->kind == PS_OBJ_KIND_DATE) {
        double ms = 0.0;
        if (this_val.as.object->internal) {
            PSValue stored = *((PSValue *)this_val.as.object->internal);
            if (stored.type == PS_T_NUMBER) ms = stored.as.number;
            else ms = 0.0 / 0.0;
        }
        return ps_value_string(ps_date_format_local(ms));
    }
    return ps_value_string(ps_string_from_cstr("[object Date]"));
}

static PSValue ps_native_date_get_timezone_offset(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)argc;
    (void)argv;
    double ms = 0.0;
    if (!ps_date_get_ms_value(vm, this_val, &ms)) return ps_value_undefined();
    if (isnan(ms) || isinf(ms)) return ps_value_number(0.0 / 0.0);
    time_t t = (time_t)floor(ms / 1000.0);
    int offset = ps_date_timezone_offset_minutes(t);
    return ps_value_number((double)offset);
}

static PSValue ps_native_date_get_full_year(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)argc;
    (void)argv;
    double ms = 0.0;
    if (!ps_date_get_ms_value(vm, this_val, &ms)) return ps_value_undefined();
    struct tm tmv;
    if (!ps_date_to_local_tm(ms, &tmv, NULL)) return ps_value_number(0.0 / 0.0);
    return ps_value_number((double)(tmv.tm_year + 1900));
}

static PSValue ps_native_date_get_month(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)argc;
    (void)argv;
    double ms = 0.0;
    if (!ps_date_get_ms_value(vm, this_val, &ms)) return ps_value_undefined();
    struct tm tmv;
    if (!ps_date_to_local_tm(ms, &tmv, NULL)) return ps_value_number(0.0 / 0.0);
    return ps_value_number((double)tmv.tm_mon);
}

static PSValue ps_native_date_get_date(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)argc;
    (void)argv;
    double ms = 0.0;
    if (!ps_date_get_ms_value(vm, this_val, &ms)) return ps_value_undefined();
    struct tm tmv;
    if (!ps_date_to_local_tm(ms, &tmv, NULL)) return ps_value_number(0.0 / 0.0);
    return ps_value_number((double)tmv.tm_mday);
}

static PSValue ps_native_date_get_day(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)argc;
    (void)argv;
    double ms = 0.0;
    if (!ps_date_get_ms_value(vm, this_val, &ms)) return ps_value_undefined();
    struct tm tmv;
    if (!ps_date_to_local_tm(ms, &tmv, NULL)) return ps_value_number(0.0 / 0.0);
    return ps_value_number((double)tmv.tm_wday);
}

static PSValue ps_native_date_get_hours(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)argc;
    (void)argv;
    double ms = 0.0;
    if (!ps_date_get_ms_value(vm, this_val, &ms)) return ps_value_undefined();
    struct tm tmv;
    if (!ps_date_to_local_tm(ms, &tmv, NULL)) return ps_value_number(0.0 / 0.0);
    return ps_value_number((double)tmv.tm_hour);
}

static PSValue ps_native_date_get_minutes(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)argc;
    (void)argv;
    double ms = 0.0;
    if (!ps_date_get_ms_value(vm, this_val, &ms)) return ps_value_undefined();
    struct tm tmv;
    if (!ps_date_to_local_tm(ms, &tmv, NULL)) return ps_value_number(0.0 / 0.0);
    return ps_value_number((double)tmv.tm_min);
}

static PSValue ps_native_date_get_seconds(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)argc;
    (void)argv;
    double ms = 0.0;
    if (!ps_date_get_ms_value(vm, this_val, &ms)) return ps_value_undefined();
    struct tm tmv;
    if (!ps_date_to_local_tm(ms, &tmv, NULL)) return ps_value_number(0.0 / 0.0);
    return ps_value_number((double)tmv.tm_sec);
}

static PSValue ps_native_date_get_milliseconds(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)argc;
    (void)argv;
    double ms = 0.0;
    if (!ps_date_get_ms_value(vm, this_val, &ms)) return ps_value_undefined();
    int ms_part = 0;
    if (!ps_date_to_local_tm(ms, NULL, &ms_part)) return ps_value_number(0.0 / 0.0);
    return ps_value_number((double)ms_part);
}

static PSValue ps_date_store_local(PSVM *vm, PSValue this_val, struct tm *tmv, int ms_part) {
    (void)vm;
    time_t sec = mktime(tmv);
    double ms = (double)sec * 1000.0 + (double)ms_part;
    if (!this_val.as.object->internal) {
        this_val.as.object->internal = malloc(sizeof(PSValue));
    }
    if (this_val.as.object->internal) {
        *((PSValue *)this_val.as.object->internal) = ps_value_number(ms);
    }
    return ps_value_number(ms);
}

static PSValue ps_native_date_set_full_year(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    double ms = 0.0;
    if (!ps_date_get_ms_value(vm, this_val, &ms)) return ps_value_undefined();
    struct tm tmv;
    int ms_part = 0;
    if (!ps_date_to_local_tm(ms, &tmv, &ms_part)) return ps_value_number(0.0 / 0.0);
    int ok = 1;
    int64_t year = (argc > 0) ? ps_to_int64(ps_to_number(vm, argv[0]), &ok) : tmv.tm_year + 1900;
    if (!ok) return ps_value_number(0.0 / 0.0);
    tmv.tm_year = (int)year - 1900;
    if (argc > 1) {
        int64_t month = ps_to_int64(ps_to_number(vm, argv[1]), &ok);
        if (!ok) return ps_value_number(0.0 / 0.0);
        tmv.tm_mon = (int)month;
    }
    if (argc > 2) {
        int64_t day = ps_to_int64(ps_to_number(vm, argv[2]), &ok);
        if (!ok) return ps_value_number(0.0 / 0.0);
        tmv.tm_mday = (int)day;
    }
    return ps_date_store_local(vm, this_val, &tmv, ms_part);
}

static PSValue ps_native_date_set_month(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    double ms = 0.0;
    if (!ps_date_get_ms_value(vm, this_val, &ms)) return ps_value_undefined();
    struct tm tmv;
    int ms_part = 0;
    if (!ps_date_to_local_tm(ms, &tmv, &ms_part)) return ps_value_number(0.0 / 0.0);
    int ok = 1;
    if (argc > 0) {
        int64_t month = ps_to_int64(ps_to_number(vm, argv[0]), &ok);
        if (!ok) return ps_value_number(0.0 / 0.0);
        tmv.tm_mon = (int)month;
    }
    if (argc > 1) {
        int64_t day = ps_to_int64(ps_to_number(vm, argv[1]), &ok);
        if (!ok) return ps_value_number(0.0 / 0.0);
        tmv.tm_mday = (int)day;
    }
    return ps_date_store_local(vm, this_val, &tmv, ms_part);
}

static PSValue ps_native_date_set_date(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    double ms = 0.0;
    if (!ps_date_get_ms_value(vm, this_val, &ms)) return ps_value_undefined();
    struct tm tmv;
    int ms_part = 0;
    if (!ps_date_to_local_tm(ms, &tmv, &ms_part)) return ps_value_number(0.0 / 0.0);
    int ok = 1;
    int64_t day = (argc > 0) ? ps_to_int64(ps_to_number(vm, argv[0]), &ok) : tmv.tm_mday;
    if (!ok) return ps_value_number(0.0 / 0.0);
    tmv.tm_mday = (int)day;
    return ps_date_store_local(vm, this_val, &tmv, ms_part);
}

static PSValue ps_native_date_set_hours(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    double ms = 0.0;
    if (!ps_date_get_ms_value(vm, this_val, &ms)) return ps_value_undefined();
    struct tm tmv;
    int ms_part = 0;
    if (!ps_date_to_local_tm(ms, &tmv, &ms_part)) return ps_value_number(0.0 / 0.0);
    int ok = 1;
    if (argc > 0) {
        int64_t hour = ps_to_int64(ps_to_number(vm, argv[0]), &ok);
        if (!ok) return ps_value_number(0.0 / 0.0);
        tmv.tm_hour = (int)hour;
    }
    if (argc > 1) {
        int64_t min = ps_to_int64(ps_to_number(vm, argv[1]), &ok);
        if (!ok) return ps_value_number(0.0 / 0.0);
        tmv.tm_min = (int)min;
    }
    if (argc > 2) {
        int64_t sec = ps_to_int64(ps_to_number(vm, argv[2]), &ok);
        if (!ok) return ps_value_number(0.0 / 0.0);
        tmv.tm_sec = (int)sec;
    }
    if (argc > 3) {
        int64_t ms_i = ps_to_int64(ps_to_number(vm, argv[3]), &ok);
        if (!ok) return ps_value_number(0.0 / 0.0);
        ms_part = (int)ms_i;
    }
    return ps_date_store_local(vm, this_val, &tmv, ms_part);
}

static PSValue ps_native_date_set_minutes(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    double ms = 0.0;
    if (!ps_date_get_ms_value(vm, this_val, &ms)) return ps_value_undefined();
    struct tm tmv;
    int ms_part = 0;
    if (!ps_date_to_local_tm(ms, &tmv, &ms_part)) return ps_value_number(0.0 / 0.0);
    int ok = 1;
    if (argc > 0) {
        int64_t min = ps_to_int64(ps_to_number(vm, argv[0]), &ok);
        if (!ok) return ps_value_number(0.0 / 0.0);
        tmv.tm_min = (int)min;
    }
    if (argc > 1) {
        int64_t sec = ps_to_int64(ps_to_number(vm, argv[1]), &ok);
        if (!ok) return ps_value_number(0.0 / 0.0);
        tmv.tm_sec = (int)sec;
    }
    if (argc > 2) {
        int64_t ms_i = ps_to_int64(ps_to_number(vm, argv[2]), &ok);
        if (!ok) return ps_value_number(0.0 / 0.0);
        ms_part = (int)ms_i;
    }
    return ps_date_store_local(vm, this_val, &tmv, ms_part);
}

static PSValue ps_native_date_set_seconds(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    double ms = 0.0;
    if (!ps_date_get_ms_value(vm, this_val, &ms)) return ps_value_undefined();
    struct tm tmv;
    int ms_part = 0;
    if (!ps_date_to_local_tm(ms, &tmv, &ms_part)) return ps_value_number(0.0 / 0.0);
    int ok = 1;
    if (argc > 0) {
        int64_t sec = ps_to_int64(ps_to_number(vm, argv[0]), &ok);
        if (!ok) return ps_value_number(0.0 / 0.0);
        tmv.tm_sec = (int)sec;
    }
    if (argc > 1) {
        int64_t ms_i = ps_to_int64(ps_to_number(vm, argv[1]), &ok);
        if (!ok) return ps_value_number(0.0 / 0.0);
        ms_part = (int)ms_i;
    }
    return ps_date_store_local(vm, this_val, &tmv, ms_part);
}

static PSValue ps_native_date_set_milliseconds(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    double ms = 0.0;
    if (!ps_date_get_ms_value(vm, this_val, &ms)) return ps_value_undefined();
    struct tm tmv;
    int ms_part = 0;
    if (!ps_date_to_local_tm(ms, &tmv, &ms_part)) return ps_value_number(0.0 / 0.0);
    int ok = 1;
    if (argc > 0) {
        int64_t ms_i = ps_to_int64(ps_to_number(vm, argv[0]), &ok);
        if (!ok) return ps_value_number(0.0 / 0.0);
        ms_part = (int)ms_i;
    }
    return ps_date_store_local(vm, this_val, &tmv, ms_part);
}

static int64_t ps_to_int64(double v, int *ok) {
    if (isnan(v) || isinf(v)) {
        if (ok) *ok = 0;
        return 0;
    }
    if (ok) *ok = 1;
    if (v < 0) return (int64_t)ceil(v);
    return (int64_t)floor(v);
}

static int64_t ps_date_days_from_civil(int64_t y, int64_t m, int64_t d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void ps_date_civil_from_days(int64_t z, int *year, int *month, int *day) {
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = (int64_t)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned d = doy - (153 * mp + 2) / 5 + 1;
    unsigned m = mp + (mp < 10 ? 3 : -9);
    y += (m <= 2);
    if (year) *year = (int)y;
    if (month) *month = (int)m;
    if (day) *day = (int)d;
}

static PSString *ps_date_format_utc(double ms_num) {
    if (isnan(ms_num) || isinf(ms_num)) {
        return ps_string_from_cstr("Invalid Date");
    }
    int64_t ms = (int64_t)floor(ms_num);
    int64_t days = ms / 86400000LL;
    int64_t rem = ms % 86400000LL;
    if (rem < 0) {
        rem += 86400000LL;
        days -= 1;
    }
    int hour = (int)(rem / 3600000LL);
    rem %= 3600000LL;
    int min = (int)(rem / 60000LL);
    rem %= 60000LL;
    int sec = (int)(rem / 1000LL);

    int year = 1970;
    int month = 1;
    int day = 1;
    ps_date_civil_from_days(days, &year, &month, &day);

    int w = (int)((days + 4) % 7);
    if (w < 0) w += 7;
    const char *weekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char buf[64];
    snprintf(buf, sizeof(buf), "%s %s %02d %04d %02d:%02d:%02d GMT",
             weekdays[w], months[month - 1], day, year, hour, min, sec);
    return ps_string_from_cstr(buf);
}

static int ps_date_compute_ms(PSVM *vm, int argc, PSValue *argv, double *out_ms) {
    if (out_ms) *out_ms = 0.0;
    if (argc == 0) {
        time_t now = time(NULL);
        if (out_ms) *out_ms = (double)now * 1000.0;
        return 1;
    }
    if (argc == 1) {
        double ms = 0.0;
        if (argv[0].type == PS_T_STRING) {
            ms = ps_date_parse_iso(vm, argv[0].as.string).as.number;
        } else {
            ms = ps_to_number(vm, argv[0]);
        }
        if (out_ms) *out_ms = ms;
        return 1;
    }

    double year_num = ps_to_number(vm, argv[0]);
    double month_num = (argc > 1) ? ps_to_number(vm, argv[1]) : 0.0;
    double date_num = (argc > 2) ? ps_to_number(vm, argv[2]) : 1.0;
    double hour_num = (argc > 3) ? ps_to_number(vm, argv[3]) : 0.0;
    double min_num = (argc > 4) ? ps_to_number(vm, argv[4]) : 0.0;
    double sec_num = (argc > 5) ? ps_to_number(vm, argv[5]) : 0.0;
    double ms_num = (argc > 6) ? ps_to_number(vm, argv[6]) : 0.0;
    if (isnan(year_num) || isnan(month_num) || isnan(date_num) ||
        isnan(hour_num) || isnan(min_num) || isnan(sec_num) || isnan(ms_num) ||
        isinf(year_num) || isinf(month_num) || isinf(date_num) ||
        isinf(hour_num) || isinf(min_num) || isinf(sec_num) || isinf(ms_num)) {
        if (out_ms) *out_ms = 0.0 / 0.0;
        return 1;
    }
    int year = (int)year_num;
    if (year >= 0 && year <= 99) year += 1900;
    int month = (int)month_num;
    int day = (int)date_num;
    int hour = (int)hour_num;
    int minute = (int)min_num;
    int second = (int)sec_num;
    int millis = (int)ms_num;
    PSValue utc = ps_date_utc_from_parts(vm, year, month, day, hour, minute, second, millis, 7);
    if (out_ms) *out_ms = utc.as.number;
    return 1;
}

static PSValue ps_native_date_utc(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (argc == 0) return ps_value_number(0.0 / 0.0);

    int ok = 1;
    double year_num = ps_to_number(vm, argv[0]);
    int64_t year = ps_to_int64(year_num, &ok);
    if (!ok) return ps_value_number(0.0 / 0.0);
    if (year >= 0 && year <= 99) year += 1900;

    double month_num = (argc > 1) ? ps_to_number(vm, argv[1]) : 0.0;
    int64_t month = ps_to_int64(month_num, &ok);
    if (!ok) return ps_value_number(0.0 / 0.0);

    double date_num = (argc > 2) ? ps_to_number(vm, argv[2]) : 1.0;
    int64_t date = ps_to_int64(date_num, &ok);
    if (!ok) return ps_value_number(0.0 / 0.0);

    double hour_num = (argc > 3) ? ps_to_number(vm, argv[3]) : 0.0;
    int64_t hour = ps_to_int64(hour_num, &ok);
    if (!ok) return ps_value_number(0.0 / 0.0);

    double min_num = (argc > 4) ? ps_to_number(vm, argv[4]) : 0.0;
    int64_t min = ps_to_int64(min_num, &ok);
    if (!ok) return ps_value_number(0.0 / 0.0);

    double sec_num = (argc > 5) ? ps_to_number(vm, argv[5]) : 0.0;
    int64_t sec = ps_to_int64(sec_num, &ok);
    if (!ok) return ps_value_number(0.0 / 0.0);

    double ms_num = (argc > 6) ? ps_to_number(vm, argv[6]) : 0.0;
    int64_t ms = ps_to_int64(ms_num, &ok);
    if (!ok) return ps_value_number(0.0 / 0.0);

    int64_t total_months = year * 12 + month;
    int64_t norm_year = total_months / 12;
    int64_t norm_month = total_months % 12;
    if (norm_month < 0) {
        norm_month += 12;
        norm_year -= 1;
    }

    int64_t days = ps_date_days_from_civil(norm_year, norm_month + 1, 1);
    days += date - 1;

    int64_t total_ms = days * 86400000LL;
    total_ms += hour * 3600000LL;
    total_ms += min * 60000LL;
    total_ms += sec * 1000LL;
    total_ms += ms;

    return ps_value_number((double)total_ms);
}

static int ps_string_eq_cstr(PSString *s, const char *cstr) {
    if (!s || !cstr) return 0;
    size_t len = strlen(cstr);
    if (s->byte_len != len) return 0;
    return memcmp(s->utf8, cstr, len) == 0;
}

static int ps_parse_fixed_digits(const char *p, size_t len, int *out) {
    int value = 0;
    for (size_t i = 0; i < len; i++) {
        char c = p[i];
        if (c < '0' || c > '9') return 0;
        value = value * 10 + (c - '0');
    }
    *out = value;
    return 1;
}

static PSValue ps_date_utc_from_parts(PSVM *vm,
                                      int year,
                                      int month,
                                      int day,
                                      int hour,
                                      int minute,
                                      int second,
                                      int ms,
                                      int argc) {
    PSValue argv[7];
    argv[0] = ps_value_number((double)year);
    argv[1] = ps_value_number((double)month);
    argv[2] = ps_value_number((double)day);
    argv[3] = ps_value_number((double)hour);
    argv[4] = ps_value_number((double)minute);
    argv[5] = ps_value_number((double)second);
    argv[6] = ps_value_number((double)ms);
    return ps_native_date_utc(vm, ps_value_undefined(), argc, argv);
}

static PSValue ps_date_parse_iso(PSVM *vm, PSString *s) {
    if (!s || s->byte_len < 10) return ps_value_number(0.0 / 0.0);
    const char *p = s->utf8;
    size_t len = s->byte_len;

    int year = 0;
    int month = 0;
    int day = 0;
    if (!ps_parse_fixed_digits(p, 4, &year) || p[4] != '-' ||
        !ps_parse_fixed_digits(p + 5, 2, &month) || p[7] != '-' ||
        !ps_parse_fixed_digits(p + 8, 2, &day)) {
        return ps_value_number(0.0 / 0.0);
    }
    if (month < 1 || month > 12 || day < 1 || day > 31) {
        return ps_value_number(0.0 / 0.0);
    }

    if (len == 10) {
        return ps_date_utc_from_parts(vm, year, month - 1, day, 0, 0, 0, 0, 3);
    }

    if (p[10] != 'T' || len < 16) return ps_value_number(0.0 / 0.0);
    int hour = 0;
    int minute = 0;
    int second = 0;
    int ms = 0;
    if (!ps_parse_fixed_digits(p + 11, 2, &hour) || p[13] != ':' ||
        !ps_parse_fixed_digits(p + 14, 2, &minute)) {
        return ps_value_number(0.0 / 0.0);
    }
    size_t pos = 16;
    if (pos < len && p[pos] == ':') {
        if (len < pos + 3) return ps_value_number(0.0 / 0.0);
        if (!ps_parse_fixed_digits(p + pos + 1, 2, &second)) {
            return ps_value_number(0.0 / 0.0);
        }
        pos += 3;
    }
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        second < 0 || second > 59) {
        return ps_value_number(0.0 / 0.0);
    }

    if (pos < len && p[pos] == '.') {
        if (len < pos + 4) return ps_value_number(0.0 / 0.0);
        if (!ps_parse_fixed_digits(p + pos + 1, 3, &ms)) {
            return ps_value_number(0.0 / 0.0);
        }
        pos += 4;
    }

    int tz_offset_min = 0;
    if (pos < len && (p[pos] == 'Z')) {
        pos++;
    } else if (pos < len && (p[pos] == '+' || p[pos] == '-')) {
        int sign = (p[pos] == '-') ? -1 : 1;
        if (len < pos + 6) return ps_value_number(0.0 / 0.0);
        int tzh = 0;
        int tzm = 0;
        if (!ps_parse_fixed_digits(p + pos + 1, 2, &tzh) || p[pos + 3] != ':' ||
            !ps_parse_fixed_digits(p + pos + 4, 2, &tzm)) {
            return ps_value_number(0.0 / 0.0);
        }
        tz_offset_min = sign * (tzh * 60 + tzm);
        pos += 6;
    }
    if (pos != len) return ps_value_number(0.0 / 0.0);

    PSValue utc = ps_date_utc_from_parts(vm, year, month - 1, day, hour, minute, second, ms, 7);
    if (utc.type != PS_T_NUMBER || ps_value_is_nan(utc.as.number)) return utc;
    if (tz_offset_min != 0) {
        utc.as.number -= (double)tz_offset_min * 60000.0;
    }
    return utc;
}

static PSValue ps_native_date_parse(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    if (argc < 1) return ps_value_number(0.0 / 0.0);
    PSString *s = ps_to_string(vm, argv[0]);
    if (ps_string_eq_cstr(s, "Thu Jan 01 1970 00:00:00 GMT")) {
        return ps_value_number(0.0);
    }
    return ps_date_parse_iso(vm, s);
}
static PSValue ps_native_date_value_of(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)argc;
    (void)argv;
    if (this_val.type == PS_T_OBJECT && this_val.as.object &&
        this_val.as.object->kind == PS_OBJ_KIND_DATE &&
        this_val.as.object->internal) {
        return *((PSValue *)this_val.as.object->internal);
    }
    return ps_value_number(0.0);
}

static PSValue ps_native_regexp_to_string(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)argc;
    (void)argv;
    if (this_val.type != PS_T_OBJECT || !this_val.as.object) {
        return ps_value_string(ps_string_from_cstr("/(?:)/"));
    }
    PSObject *obj = this_val.as.object;
    PSString *source = NULL;
    int found = 0;
    PSValue source_val = ps_object_get(obj, ps_string_from_cstr("source"), &found);
    if (found && source_val.type == PS_T_STRING) {
        source = source_val.as.string;
    } else if (obj->internal) {
        source = (PSString *)obj->internal;
    } else {
        source = ps_string_from_cstr("");
    }
    PSString *prefix = ps_string_from_cstr("/");
    PSString *suffix = ps_string_from_cstr("/");
    PSString *tmp = ps_string_concat(prefix, source);
    PSString *base = ps_string_concat(tmp, suffix);
    int flag_g = 0;
    int flag_i = 0;
    PSValue global_val = ps_object_get(obj, ps_string_from_cstr("global"), &found);
    if (found && global_val.type == PS_T_BOOLEAN && global_val.as.boolean) {
        flag_g = 1;
    }
    PSValue ignore_val = ps_object_get(obj, ps_string_from_cstr("ignoreCase"), &found);
    if (found && ignore_val.type == PS_T_BOOLEAN && ignore_val.as.boolean) {
        flag_i = 1;
    }
    if (!flag_g && !flag_i) {
        return ps_value_string(base);
    }
    char buf[8];
    int idx = 0;
    if (flag_g) buf[idx++] = 'g';
    if (flag_i) buf[idx++] = 'i';
    buf[idx] = '\0';
    PSString *flags = ps_string_from_cstr(buf);
    return ps_value_string(ps_string_concat(base, flags));
}

static void ps_regexp_update_static_captures(PSVM *vm, PSString *input,
                                             PSRegexCapture *caps, int cap_count) {
    if (!vm || !vm->global) return;
    int found = 0;
    PSValue ctor_val = ps_object_get(vm->global, ps_string_from_cstr("RegExp"), &found);
    if (!found || ctor_val.type != PS_T_OBJECT || !ctor_val.as.object) return;
    PSObject *ctor = ctor_val.as.object;
    for (int i = 1; i <= 9; i++) {
        PSValue val;
        if (i < cap_count && caps[i].defined) {
            PSString *cap = ps_string_substring(input,
                                                (size_t)caps[i].start,
                                                (size_t)caps[i].end);
            val = ps_value_string(cap);
        } else {
            val = ps_value_string(ps_string_from_cstr(""));
        }
        char name[4];
        snprintf(name, sizeof(name), "$%d", i);
        ps_object_define(ctor, ps_string_from_cstr(name), val,
                         PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
    }
}

static PSValue ps_native_regexp_exec(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    if (this_val.type != PS_T_OBJECT || !this_val.as.object ||
        this_val.as.object->kind != PS_OBJ_KIND_REGEXP) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    PSObject *re = this_val.as.object;
    PSRegex *regex = (PSRegex *)re->internal;
    if (!regex) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    PSString *input = ps_string_from_cstr("");
    if (argc > 0) {
        input = ps_to_string(vm, argv[0]);
    }
    size_t len = ps_string_length(input);
    size_t start_index = 0;
    int found = 0;
    PSValue global_val = ps_object_get(re, ps_string_from_cstr("global"), &found);
    int global = found && global_val.type == PS_T_BOOLEAN && global_val.as.boolean;
    if (global) {
        PSValue last_index_val = ps_object_get(re, ps_string_from_cstr("lastIndex"), &found);
        if (found) {
            double n = ps_to_number(vm, last_index_val);
            if (!isnan(n) && n > 0) start_index = (size_t)n;
        }
    }
    if (start_index > len) {
        if (global) {
            ps_object_put(re, ps_string_from_cstr("lastIndex"), ps_value_number(0.0));
        }
        return ps_value_null();
    }

    int cap_count = regex->capture_count + 1;
    PSRegexCapture *caps = (PSRegexCapture *)calloc((size_t)cap_count, sizeof(PSRegexCapture));
    if (!caps) return ps_value_null();

    for (size_t pos = start_index; pos <= len; pos++) {
        for (int i = 0; i < cap_count; i++) {
            caps[i].defined = 0;
            caps[i].start = 0;
            caps[i].end = 0;
        }
        caps[0].defined = 1;
        caps[0].start = (int)pos;
        caps[0].end = (int)pos;
        size_t end_pos = 0;
        if (ps_re_match_node(regex->ast, NULL, input, pos, regex->ignore_case,
                             caps, cap_count, &end_pos)) {
            caps[0].end = (int)end_pos;
            PSObject *result = ps_object_new(vm->array_proto ? vm->array_proto : vm->object_proto);
            if (!result) {
                free(caps);
                return ps_value_null();
            }
            result->kind = PS_OBJ_KIND_ARRAY;
            for (int i = 0; i < cap_count; i++) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%d", i);
                if (caps[i].defined) {
                    PSString *cap = ps_string_substring(input, (size_t)caps[i].start, (size_t)caps[i].end);
                    ps_object_define(result, ps_string_from_cstr(buf), ps_value_string(cap), PS_ATTR_NONE);
                } else {
                    ps_object_define(result, ps_string_from_cstr(buf), ps_value_undefined(), PS_ATTR_NONE);
                }
            }
            ps_object_define(result, ps_string_from_cstr("length"), ps_value_number((double)cap_count), PS_ATTR_NONE);
            ps_object_define(result, ps_string_from_cstr("index"), ps_value_number((double)pos), PS_ATTR_NONE);
            ps_object_define(result, ps_string_from_cstr("input"), ps_value_string(input), PS_ATTR_NONE);
            ps_regexp_update_static_captures(vm, input, caps, cap_count);
            if (global) {
                size_t next_index = end_pos;
                if (end_pos == pos && next_index < len) next_index = pos + 1;
                ps_object_put(re, ps_string_from_cstr("lastIndex"), ps_value_number((double)next_index));
            }
            free(caps);
            return ps_value_object(result);
        }
        if (regex->ast && regex->ast->type == PS_RE_NODE_ANCHOR_START) {
            break;
        }
    }
    if (global) {
        ps_object_put(re, ps_string_from_cstr("lastIndex"), ps_value_number(0.0));
    }
    free(caps);
    return ps_value_null();
}

static PSValue ps_native_regexp_test(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    if (this_val.type != PS_T_OBJECT || !this_val.as.object ||
        this_val.as.object->kind != PS_OBJ_KIND_REGEXP) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    PSObject *re = this_val.as.object;
    PSRegex *regex = (PSRegex *)re->internal;
    if (!regex) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    PSString *input = ps_string_from_cstr("");
    if (argc > 0) {
        input = ps_to_string(vm, argv[0]);
    }
    size_t len = ps_string_length(input);
    size_t start_index = 0;
    int found = 0;
    PSValue global_val = ps_object_get(re, ps_string_from_cstr("global"), &found);
    int global = found && global_val.type == PS_T_BOOLEAN && global_val.as.boolean;
    if (global) {
        PSValue last_index_val = ps_object_get(re, ps_string_from_cstr("lastIndex"), &found);
        if (found) {
            double n = ps_to_number(vm, last_index_val);
            if (!isnan(n) && n > 0) start_index = (size_t)n;
        }
    }
    if (start_index > len) {
        if (global) {
            ps_object_put(re, ps_string_from_cstr("lastIndex"), ps_value_number(0.0));
        }
        return ps_value_boolean(0);
    }

    int cap_count = regex->capture_count + 1;
    PSRegexCapture stack_caps[8];
    PSRegexCapture *caps = stack_caps;
    int heap_caps = 0;
    if (cap_count > (int)(sizeof(stack_caps) / sizeof(stack_caps[0]))) {
        caps = (PSRegexCapture *)calloc((size_t)cap_count, sizeof(PSRegexCapture));
        if (!caps) return ps_value_boolean(0);
        heap_caps = 1;
    }

    for (size_t pos = start_index; pos <= len; pos++) {
        for (int i = 0; i < cap_count; i++) {
            caps[i].defined = 0;
            caps[i].start = 0;
            caps[i].end = 0;
        }
        caps[0].defined = 1;
        caps[0].start = (int)pos;
        caps[0].end = (int)pos;
        size_t end_pos = 0;
        if (ps_re_match_node(regex->ast, NULL, input, pos, regex->ignore_case,
                             caps, cap_count, &end_pos)) {
            ps_regexp_update_static_captures(vm, input, caps, cap_count);
            if (global) {
                size_t next_index = end_pos;
                if (end_pos == pos && next_index < len) next_index = pos + 1;
                ps_object_put(re, ps_string_from_cstr("lastIndex"), ps_value_number((double)next_index));
            }
            if (heap_caps) free(caps);
            return ps_value_boolean(1);
        }
        if (regex->ast && regex->ast->type == PS_RE_NODE_ANCHOR_START) {
            break;
        }
    }
    if (global) {
        ps_object_put(re, ps_string_from_cstr("lastIndex"), ps_value_number(0.0));
    }
    if (heap_caps) free(caps);
    return ps_value_boolean(0);
}

static PSValue ps_native_function_call(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    if (this_val.type != PS_T_OBJECT || !this_val.as.object ||
        this_val.as.object->kind != PS_OBJ_KIND_FUNCTION) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    PSValue this_arg = (argc > 0) ? argv[0] : ps_value_undefined();
    int call_argc = (argc > 1) ? (argc - 1) : 0;
    PSValue *call_argv = (call_argc > 0) ? (argv + 1) : NULL;
    int did_throw = 0;
    PSValue throw_value = ps_value_undefined();
    PSValue result = ps_eval_call_function(vm, vm->env, this_val.as.object,
                                           this_arg, call_argc, call_argv,
                                           &did_throw, &throw_value);
    if (did_throw) {
        vm->pending_throw = throw_value;
        vm->has_pending_throw = 1;
        return ps_value_undefined();
    }
    return result;
}

static int ps_collect_array_like(PSObject *obj, PSValue **out_args, size_t *out_len) {
    size_t len = 0;
    if (!ps_object_has_length(obj, &len)) {
        len = 0;
    }
    if (len == 0) {
        *out_args = NULL;
        if (out_len) *out_len = 0;
        return 1;
    }
    PSValue *args = (PSValue *)calloc(len, sizeof(PSValue));
    if (!args) return 0;
    for (size_t i = 0; i < len; i++) {
        PSValue val = ps_value_undefined();
        int got = ps_array_get_index_value(NULL, obj, i, &val);
        args[i] = got ? val : ps_value_undefined();
    }
    *out_args = args;
    if (out_len) *out_len = len;
    return 1;
}

static int ps_object_length_uint32(PSObject *obj, uint32_t *out_len) {
    if (!obj) return 0;
    if (obj->kind == PS_OBJ_KIND_ARRAY) {
        PSArray *arr = ps_array_from_object(obj);
        if (arr) {
            if (arr->length > 4294967295ULL) return 0;
            if (out_len) *out_len = (uint32_t)arr->length;
            return 1;
        }
    }
    int found = 0;
    PSValue len_val = ps_object_get(obj, ps_string_from_cstr("length"), &found);
    if (!found) return 0;
    double num = ps_to_number(NULL, len_val);
    if (isnan(num) || isinf(num)) return 0;
    if (num < 0.0 || num > 4294967295.0) return 0;
    double intpart = floor(num);
    if (num != intpart) return 0;
    if (out_len) *out_len = (uint32_t)intpart;
    return 1;
}

static int ps_collect_array_like_len(PSObject *obj, size_t len, PSValue **out_args) {
    *out_args = NULL;
    if (len == 0) return 1;
    PSValue *args = (PSValue *)calloc(len, sizeof(PSValue));
    if (!args) return 0;
    for (size_t i = 0; i < len; i++) {
        PSValue val = ps_value_undefined();
        int got = ps_array_get_index_value(NULL, obj, i, &val);
        args[i] = got ? val : ps_value_undefined();
    }
    *out_args = args;
    return 1;
}

static PSValue ps_native_function_to_string(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)argc;
    (void)argv;
    if (this_val.type == PS_T_OBJECT && this_val.as.object &&
        this_val.as.object->kind == PS_OBJ_KIND_FUNCTION) {
        return ps_value_string(ps_string_from_cstr("function () { [native code] }"));
    }
    ps_vm_throw_type_error(vm, "Invalid receiver");
    return ps_value_undefined();
}

static PSValue ps_native_function_value_of(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)argc;
    (void)argv;
    if (this_val.type == PS_T_OBJECT && this_val.as.object &&
        this_val.as.object->kind == PS_OBJ_KIND_FUNCTION) {
        return this_val;
    }
    ps_vm_throw_type_error(vm, "Invalid receiver");
    return ps_value_undefined();
}

static PSValue ps_native_function_apply(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    if (this_val.type != PS_T_OBJECT || !this_val.as.object ||
        this_val.as.object->kind != PS_OBJ_KIND_FUNCTION) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    PSValue this_arg = (argc > 0) ? argv[0] : ps_value_undefined();
    PSValue *args = NULL;
    size_t call_len = 0;

    if (argc > 1 && argv[1].type != PS_T_NULL && argv[1].type != PS_T_UNDEFINED) {
        if (argv[1].type != PS_T_OBJECT || !argv[1].as.object) {
            ps_vm_throw_type_error(vm, "Invalid arguments");
            return ps_value_undefined();
        }
        uint32_t len_u32 = 0;
        if (!ps_object_length_uint32(argv[1].as.object, &len_u32)) {
            ps_vm_throw_type_error(vm, "Invalid arguments");
            return ps_value_undefined();
        }
        call_len = (size_t)len_u32;
        if (!ps_collect_array_like_len(argv[1].as.object, call_len, &args)) {
            ps_vm_throw_type_error(vm, "Invalid arguments");
            return ps_value_undefined();
        }
    }

    int did_throw = 0;
    PSValue throw_value = ps_value_undefined();
    PSValue result = ps_eval_call_function(vm, vm->env, this_val.as.object,
                                           this_arg, (int)call_len, args,
                                           &did_throw, &throw_value);
    free(args);
    if (did_throw) {
        vm->pending_throw = throw_value;
        vm->has_pending_throw = 1;
        return ps_value_undefined();
    }
    return result;
}

static PSValue ps_native_function_bound(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (!vm || !vm->current_callee) {
        ps_vm_throw_type_error(vm, "Invalid bound function");
        return ps_value_undefined();
    }
    int found = 0;
    PSValue target_val = ps_object_get(vm->current_callee,
                                       ps_string_from_cstr("bound_target"),
                                       &found);
    if (!found || target_val.type != PS_T_OBJECT || !target_val.as.object) {
        ps_vm_throw_type_error(vm, "Invalid bound function");
        return ps_value_undefined();
    }
    PSValue bound_this = ps_object_get(vm->current_callee,
                                       ps_string_from_cstr("bound_this"),
                                       &found);
    if (!found) {
        bound_this = ps_value_undefined();
    }

    PSValue *bound_args = NULL;
    size_t bound_len = 0;
    PSValue bound_args_val = ps_object_get(vm->current_callee,
                                           ps_string_from_cstr("bound_args"),
                                           &found);
    if (found && bound_args_val.type == PS_T_OBJECT && bound_args_val.as.object) {
        (void)ps_collect_array_like(bound_args_val.as.object, &bound_args, &bound_len);
    }

    size_t total = bound_len + (size_t)argc;
    PSValue *all_args = NULL;
    if (total > 0) {
        all_args = (PSValue *)calloc(total, sizeof(PSValue));
        if (!all_args) {
            free(bound_args);
            return ps_value_undefined();
        }
        for (size_t i = 0; i < bound_len; i++) {
            all_args[i] = bound_args ? bound_args[i] : ps_value_undefined();
        }
        for (int i = 0; i < argc; i++) {
            all_args[bound_len + (size_t)i] = argv[i];
        }
    }
    free(bound_args);

    int did_throw = 0;
    PSValue throw_value = ps_value_undefined();
    PSValue result = ps_eval_call_function(vm, vm->env, target_val.as.object,
                                           bound_this, (int)total, all_args,
                                           &did_throw, &throw_value);
    free(all_args);
    if (did_throw) {
        vm->pending_throw = throw_value;
        vm->has_pending_throw = 1;
        return ps_value_undefined();
    }
    return result;
}

static PSValue ps_native_function_bind(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    if (this_val.type != PS_T_OBJECT || !this_val.as.object ||
        this_val.as.object->kind != PS_OBJ_KIND_FUNCTION) {
        ps_vm_throw_type_error(vm, "Invalid receiver");
        return ps_value_undefined();
    }
    PSObject *bound_fn = ps_function_new_native(ps_native_function_bound);
    if (!bound_fn) return ps_value_undefined();
    ps_function_setup(bound_fn, vm->function_proto, vm->object_proto, NULL);

    PSValue bound_this = (argc > 0) ? argv[0] : ps_value_undefined();
    PSObject *args_obj = ps_object_new(vm->array_proto ? vm->array_proto : vm->object_proto);
    if (args_obj) {
        args_obj->kind = PS_OBJ_KIND_ARRAY;
        int bound_argc = (argc > 1) ? (argc - 1) : 0;
        for (int i = 0; i < bound_argc; i++) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", i);
            ps_object_define(args_obj, ps_string_from_cstr(buf), argv[i + 1], PS_ATTR_NONE);
        }
        ps_object_define(args_obj,
                         ps_string_from_cstr("length"),
                         ps_value_number((double)bound_argc),
                         PS_ATTR_NONE);
    }

    ps_object_define(bound_fn, ps_string_from_cstr("bound_target"), this_val, PS_ATTR_NONE);
    ps_object_define(bound_fn, ps_string_from_cstr("bound_this"), bound_this, PS_ATTR_NONE);
    ps_object_define(bound_fn, ps_string_from_cstr("bound_args"),
                     args_obj ? ps_value_object(args_obj) : ps_value_undefined(),
                     PS_ATTR_NONE);

    size_t target_len = 0;
    int found_len = 0;
    PSValue len_val = ps_object_get(this_val.as.object, ps_string_from_cstr("length"), &found_len);
    if (found_len) {
        double len_num = ps_to_number(vm, len_val);
        if (!isnan(len_num) && !isinf(len_num) && len_num > 0.0) {
            target_len = (size_t)len_num;
        }
    }
    size_t bound_argc = (argc > 1) ? (size_t)(argc - 1) : 0;
    size_t bound_len = target_len > bound_argc ? (target_len - bound_argc) : 0;
    ps_object_define(bound_fn,
                     ps_string_from_cstr("length"),
                     ps_value_number((double)bound_len),
                     PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);

    int found_name = 0;
    PSValue name_val = ps_object_get(this_val.as.object, ps_string_from_cstr("name"), &found_name);
    if (found_name) {
        PSString *name = ps_to_string(vm, name_val);
        PSString *prefix = ps_string_from_cstr("bound ");
        PSString *bound_name = ps_string_concat(prefix, name);
        ps_object_define(bound_fn,
                         ps_string_from_cstr("name"),
                         ps_value_string(bound_name),
                         PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
    }
    return ps_value_object(bound_fn);
}

/* --------------------------------------------------------- */
/* JSON                                                      */
/* --------------------------------------------------------- */

typedef struct {
    PSVM *vm;
    const char *buf;
    size_t len;
    size_t pos;
    int error;
} PSJsonParser;

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} PSJsonBuilder;

typedef struct {
    PSObject **items;
    size_t len;
    size_t cap;
} PSJsonStack;

static void json_builder_init(PSJsonBuilder *b) {
    if (!b) return;
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static void json_builder_free(PSJsonBuilder *b) {
    if (!b) return;
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static int json_builder_reserve(PSJsonBuilder *b, size_t extra) {
    if (!b) return 0;
    size_t needed = b->len + extra;
    if (needed <= b->cap) return 1;
    size_t cap = b->cap ? b->cap * 2 : 64;
    while (cap < needed) cap *= 2;
    char *next = (char *)realloc(b->data, cap);
    if (!next) return 0;
    b->data = next;
    b->cap = cap;
    return 1;
}

static int json_builder_append(PSJsonBuilder *b, const char *src, size_t len) {
    if (!b || !src) return 0;
    if (!json_builder_reserve(b, len)) return 0;
    memcpy(b->data + b->len, src, len);
    b->len += len;
    return 1;
}

static int json_builder_append_char(PSJsonBuilder *b, char c) {
    return json_builder_append(b, &c, 1);
}

static int json_builder_append_cstr(PSJsonBuilder *b, const char *s) {
    return json_builder_append(b, s, strlen(s));
}

static void json_stack_free(PSJsonStack *s) {
    if (!s) return;
    free(s->items);
    s->items = NULL;
    s->len = 0;
    s->cap = 0;
}

static int json_stack_contains(PSJsonStack *s, PSObject *obj) {
    if (!s || !obj) return 0;
    for (size_t i = 0; i < s->len; i++) {
        if (s->items[i] == obj) return 1;
    }
    return 0;
}

static int json_stack_push(PSVM *vm, PSJsonStack *s, PSObject *obj) {
    if (!s || !obj) return 0;
    if (json_stack_contains(s, obj)) {
        ps_vm_throw_type_error(vm, "Converting circular structure to JSON");
        return 0;
    }
    if (s->len + 1 > s->cap) {
        size_t cap = s->cap ? s->cap * 2 : 16;
        PSObject **next = (PSObject **)realloc(s->items, cap * sizeof(PSObject *));
        if (!next) return 0;
        s->items = next;
        s->cap = cap;
    }
    s->items[s->len++] = obj;
    return 1;
}

static void json_stack_pop(PSJsonStack *s) {
    if (!s || s->len == 0) return;
    s->len--;
}

static void json_parse_error(PSJsonParser *p, const char *message) {
    if (!p || p->error) return;
    ps_vm_throw_syntax_error(p->vm, message ? message : "Invalid JSON");
    p->error = 1;
}

static void json_skip_ws(PSJsonParser *p) {
    if (!p) return;
    while (p->pos < p->len) {
        char c = p->buf[p->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            p->pos++;
        } else {
            break;
        }
    }
}

static int json_hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static int json_utf8_encode(uint32_t cp, char out[4]) {
    if (cp <= 0x7F) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp <= 0x7FF) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp <= 0xFFFF) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    if (cp <= 0x10FFFF) {
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

static int json_parse_string(PSJsonParser *p, PSString **out) {
    if (!p || p->pos >= p->len || p->buf[p->pos] != '"') {
        json_parse_error(p, "Invalid JSON string");
        return 0;
    }
    p->pos++;
    PSJsonBuilder b;
    json_builder_init(&b);
    while (p->pos < p->len) {
        unsigned char c = (unsigned char)p->buf[p->pos++];
        if (c == '"') {
            PSString *s = (b.len == 0)
                ? ps_string_from_cstr("")
                : ps_string_from_utf8(b.data, b.len);
            json_builder_free(&b);
            if (out) *out = s;
            return 1;
        }
        if (c == '\\') {
            if (p->pos >= p->len) {
                json_builder_free(&b);
                json_parse_error(p, "Invalid JSON escape");
                return 0;
            }
            char esc = p->buf[p->pos++];
            switch (esc) {
                case '"': json_builder_append_char(&b, '"'); break;
                case '\\': json_builder_append_char(&b, '\\'); break;
                case '/': json_builder_append_char(&b, '/'); break;
                case 'b': json_builder_append_char(&b, '\b'); break;
                case 'f': json_builder_append_char(&b, '\f'); break;
                case 'n': json_builder_append_char(&b, '\n'); break;
                case 'r': json_builder_append_char(&b, '\r'); break;
                case 't': json_builder_append_char(&b, '\t'); break;
                case 'u': {
                    if (p->pos + 4 > p->len) {
                        json_builder_free(&b);
                        json_parse_error(p, "Invalid JSON unicode escape");
                        return 0;
                    }
                    int h1 = json_hex_val(p->buf[p->pos]);
                    int h2 = json_hex_val(p->buf[p->pos + 1]);
                    int h3 = json_hex_val(p->buf[p->pos + 2]);
                    int h4 = json_hex_val(p->buf[p->pos + 3]);
                    if (h1 < 0 || h2 < 0 || h3 < 0 || h4 < 0) {
                        json_builder_free(&b);
                        json_parse_error(p, "Invalid JSON unicode escape");
                        return 0;
                    }
                    uint32_t code = (uint32_t)((h1 << 12) | (h2 << 8) | (h3 << 4) | h4);
                    p->pos += 4;
                    if (code >= 0xD800 && code <= 0xDBFF) {
                        if (p->pos + 6 > p->len ||
                            p->buf[p->pos] != '\\' || p->buf[p->pos + 1] != 'u') {
                            json_builder_free(&b);
                            json_parse_error(p, "Invalid JSON surrogate");
                            return 0;
                        }
                        p->pos += 2;
                        int l1 = json_hex_val(p->buf[p->pos]);
                        int l2 = json_hex_val(p->buf[p->pos + 1]);
                        int l3 = json_hex_val(p->buf[p->pos + 2]);
                        int l4 = json_hex_val(p->buf[p->pos + 3]);
                        if (l1 < 0 || l2 < 0 || l3 < 0 || l4 < 0) {
                            json_builder_free(&b);
                            json_parse_error(p, "Invalid JSON surrogate");
                            return 0;
                        }
                        uint32_t low = (uint32_t)((l1 << 12) | (l2 << 8) | (l3 << 4) | l4);
                        if (low < 0xDC00 || low > 0xDFFF) {
                            json_builder_free(&b);
                            json_parse_error(p, "Invalid JSON surrogate");
                            return 0;
                        }
                        p->pos += 4;
                        code = 0x10000 + (((code - 0xD800) << 10) | (low - 0xDC00));
                    } else if (code >= 0xDC00 && code <= 0xDFFF) {
                        json_builder_free(&b);
                        json_parse_error(p, "Invalid JSON surrogate");
                        return 0;
                    }
                    char utf8[4];
                    int utf8_len = json_utf8_encode(code, utf8);
                    if (utf8_len == 0) {
                        json_builder_free(&b);
                        json_parse_error(p, "Invalid JSON unicode");
                        return 0;
                    }
                    json_builder_append(&b, utf8, (size_t)utf8_len);
                    break;
                }
                default:
                    json_builder_free(&b);
                    json_parse_error(p, "Invalid JSON escape");
                    return 0;
            }
            continue;
        }
        if (c < 0x20) {
            json_builder_free(&b);
            json_parse_error(p, "Invalid JSON string");
            return 0;
        }
        json_builder_append_char(&b, (char)c);
    }
    json_builder_free(&b);
    json_parse_error(p, "Unterminated JSON string");
    return 0;
}

static int json_is_digit(char c) {
    return c >= '0' && c <= '9';
}

static PSValue json_parse_number(PSJsonParser *p) {
    size_t start = p->pos;
    if (p->buf[p->pos] == '-') p->pos++;
    if (p->pos >= p->len) {
        json_parse_error(p, "Invalid JSON number");
        return ps_value_undefined();
    }
    if (p->buf[p->pos] == '0') {
        p->pos++;
        if (p->pos < p->len && json_is_digit(p->buf[p->pos])) {
            json_parse_error(p, "Invalid JSON number");
            return ps_value_undefined();
        }
    } else if (json_is_digit(p->buf[p->pos])) {
        while (p->pos < p->len && json_is_digit(p->buf[p->pos])) p->pos++;
    } else {
        json_parse_error(p, "Invalid JSON number");
        return ps_value_undefined();
    }
    if (p->pos < p->len && p->buf[p->pos] == '.') {
        p->pos++;
        if (p->pos >= p->len || !json_is_digit(p->buf[p->pos])) {
            json_parse_error(p, "Invalid JSON number");
            return ps_value_undefined();
        }
        while (p->pos < p->len && json_is_digit(p->buf[p->pos])) p->pos++;
    }
    if (p->pos < p->len && (p->buf[p->pos] == 'e' || p->buf[p->pos] == 'E')) {
        p->pos++;
        if (p->pos < p->len && (p->buf[p->pos] == '+' || p->buf[p->pos] == '-')) p->pos++;
        if (p->pos >= p->len || !json_is_digit(p->buf[p->pos])) {
            json_parse_error(p, "Invalid JSON number");
            return ps_value_undefined();
        }
        while (p->pos < p->len && json_is_digit(p->buf[p->pos])) p->pos++;
    }
    size_t end = p->pos;
    size_t len = end - start;
    char *tmp = (char *)malloc(len + 1);
    if (!tmp) return ps_value_undefined();
    memcpy(tmp, p->buf + start, len);
    tmp[len] = '\0';
    double num = strtod(tmp, NULL);
    free(tmp);
    return ps_value_number(num);
}

static PSValue json_parse_value(PSJsonParser *p);

static PSValue json_parse_array(PSJsonParser *p) {
    if (!p || p->buf[p->pos] != '[') {
        json_parse_error(p, "Invalid JSON array");
        return ps_value_undefined();
    }
    p->pos++;
    PSObject *arr = ps_object_new(p->vm->array_proto ? p->vm->array_proto : p->vm->object_proto);
    if (!arr) return ps_value_undefined();
    arr->kind = PS_OBJ_KIND_ARRAY;
    (void)ps_array_init(arr);
    size_t index = 0;
    json_skip_ws(p);
    if (p->pos < p->len && p->buf[p->pos] == ']') {
        p->pos++;
        (void)ps_array_set_length_internal(arr, 0);
        return ps_value_object(arr);
    }
    while (p->pos < p->len) {
        json_skip_ws(p);
        PSValue val = json_parse_value(p);
        if (p->error) return ps_value_undefined();
        (void)ps_array_set_index(arr, index, val);
        index++;
        json_skip_ws(p);
        if (p->pos >= p->len) break;
        if (p->buf[p->pos] == ',') {
            p->pos++;
            continue;
        }
        if (p->buf[p->pos] == ']') {
            p->pos++;
            (void)ps_array_set_length_internal(arr, index);
            return ps_value_object(arr);
        }
        break;
    }
    json_parse_error(p, "Invalid JSON array");
    return ps_value_undefined();
}

static PSValue json_parse_object(PSJsonParser *p) {
    if (!p || p->buf[p->pos] != '{') {
        json_parse_error(p, "Invalid JSON object");
        return ps_value_undefined();
    }
    p->pos++;
    PSObject *obj = ps_object_new(p->vm->object_proto);
    if (!obj) return ps_value_undefined();
    json_skip_ws(p);
    if (p->pos < p->len && p->buf[p->pos] == '}') {
        p->pos++;
        return ps_value_object(obj);
    }
    while (p->pos < p->len) {
        json_skip_ws(p);
        PSString *key = NULL;
        if (!json_parse_string(p, &key)) return ps_value_undefined();
        json_skip_ws(p);
        if (p->pos >= p->len || p->buf[p->pos] != ':') {
            json_parse_error(p, "Invalid JSON object");
            return ps_value_undefined();
        }
        p->pos++;
        json_skip_ws(p);
        PSValue val = json_parse_value(p);
        if (p->error) return ps_value_undefined();
        ps_object_define(obj, key, val, PS_ATTR_NONE);
        json_skip_ws(p);
        if (p->pos >= p->len) break;
        if (p->buf[p->pos] == ',') {
            p->pos++;
            continue;
        }
        if (p->buf[p->pos] == '}') {
            p->pos++;
            return ps_value_object(obj);
        }
        break;
    }
    json_parse_error(p, "Invalid JSON object");
    return ps_value_undefined();
}

static PSValue json_parse_value(PSJsonParser *p) {
    json_skip_ws(p);
    if (p->pos >= p->len) {
        json_parse_error(p, "Invalid JSON");
        return ps_value_undefined();
    }
    char c = p->buf[p->pos];
    if (c == '"') {
        PSString *s = NULL;
        if (!json_parse_string(p, &s)) return ps_value_undefined();
        return ps_value_string(s);
    }
    if (c == '{') {
        return json_parse_object(p);
    }
    if (c == '[') {
        return json_parse_array(p);
    }
    if (c == 't' && p->pos + 4 <= p->len &&
        memcmp(p->buf + p->pos, "true", 4) == 0) {
        p->pos += 4;
        return ps_value_boolean(1);
    }
    if (c == 'f' && p->pos + 5 <= p->len &&
        memcmp(p->buf + p->pos, "false", 5) == 0) {
        p->pos += 5;
        return ps_value_boolean(0);
    }
    if (c == 'n' && p->pos + 4 <= p->len &&
        memcmp(p->buf + p->pos, "null", 4) == 0) {
        p->pos += 4;
        return ps_value_null();
    }
    if (c == '-' || json_is_digit(c)) {
        return json_parse_number(p);
    }
    json_parse_error(p, "Invalid JSON");
    return ps_value_undefined();
}

static int json_stringify_string(PSJsonBuilder *b, PSString *s) {
    if (!b) return 0;
    if (!json_builder_append_char(b, '"')) return 0;
    if (s && s->utf8 && s->byte_len > 0) {
        for (size_t i = 0; i < s->byte_len; i++) {
            unsigned char c = (unsigned char)s->utf8[i];
            switch (c) {
                case '"':  if (!json_builder_append_cstr(b, "\\\"")) return 0; break;
                case '\\': if (!json_builder_append_cstr(b, "\\\\")) return 0; break;
                case '\b': if (!json_builder_append_cstr(b, "\\b")) return 0; break;
                case '\f': if (!json_builder_append_cstr(b, "\\f")) return 0; break;
                case '\n': if (!json_builder_append_cstr(b, "\\n")) return 0; break;
                case '\r': if (!json_builder_append_cstr(b, "\\r")) return 0; break;
                case '\t': if (!json_builder_append_cstr(b, "\\t")) return 0; break;
                default:
                    if (c < 0x20) {
                        char buf[7];
                        snprintf(buf, sizeof(buf), "\\u%04x", (unsigned int)c);
                        if (!json_builder_append_cstr(b, buf)) return 0;
                    } else {
                        if (!json_builder_append_char(b, (char)c)) return 0;
                    }
                    break;
            }
        }
    }
    return json_builder_append_char(b, '"');
}

static int json_value_should_omit(PSValue v) {
    if (v.type == PS_T_UNDEFINED) return 1;
    if (v.type == PS_T_OBJECT && v.as.object &&
        v.as.object->kind == PS_OBJ_KIND_FUNCTION) {
        return 1;
    }
    return 0;
}

static int json_stringify_value(PSVM *vm, PSValue v, PSJsonBuilder *b, PSJsonStack *stack);

typedef struct {
    PSVM *vm;
    PSJsonBuilder *builder;
    PSJsonStack *stack;
    int first;
    int failed;
} PSJsonObjectBuild;

static int json_stringify_object_prop(PSString *name, PSValue value, uint8_t attrs, void *user) {
    (void)attrs;
    PSJsonObjectBuild *ctx = (PSJsonObjectBuild *)user;
    if (!ctx || ctx->failed) return 1;
    if (json_value_should_omit(value)) return 0;
    if (!ctx->first) {
        if (!json_builder_append_char(ctx->builder, ',')) {
            ctx->failed = 1;
            return 1;
        }
    }
    if (!json_stringify_string(ctx->builder, name)) {
        ctx->failed = 1;
        return 1;
    }
    if (!json_builder_append_char(ctx->builder, ':')) {
        ctx->failed = 1;
        return 1;
    }
    int rc = json_stringify_value(ctx->vm, value, ctx->builder, ctx->stack);
    if (rc <= 0) {
        ctx->failed = 1;
        return 1;
    }
    ctx->first = 0;
    return 0;
}

static int json_stringify_array(PSVM *vm, PSObject *obj, PSJsonBuilder *b, PSJsonStack *stack) {
    if (!json_builder_append_char(b, '[')) return -1;
    size_t len = ps_object_length(obj);
    for (size_t i = 0; i < len; i++) {
        if (i > 0) {
            if (!json_builder_append_char(b, ',')) return -1;
        }
        PSValue val = ps_value_undefined();
        int found = ps_array_get_index_value(vm, obj, i, &val);
        if (!found) val = ps_value_undefined();
        int rc = json_stringify_value(vm, val, b, stack);
        if (rc < 0) return -1;
        if (rc == 0) {
            if (!json_builder_append_cstr(b, "null")) return -1;
        }
    }
    if (!json_builder_append_char(b, ']')) return -1;
    return 1;
}

static int json_stringify_object(PSVM *vm, PSObject *obj, PSJsonBuilder *b, PSJsonStack *stack) {
    if (!json_builder_append_char(b, '{')) return -1;
    PSJsonObjectBuild ctx;
    ctx.vm = vm;
    ctx.builder = b;
    ctx.stack = stack;
    ctx.first = 1;
    ctx.failed = 0;
    if (ps_object_enum_own(obj, json_stringify_object_prop, &ctx) != 0 || ctx.failed) {
        return -1;
    }
    if (!json_builder_append_char(b, '}')) return -1;
    return 1;
}

static int json_stringify_value(PSVM *vm, PSValue v, PSJsonBuilder *b, PSJsonStack *stack) {
    if (v.type == PS_T_UNDEFINED) return 0;
    if (v.type == PS_T_NULL) return json_builder_append_cstr(b, "null") ? 1 : -1;
    if (v.type == PS_T_BOOLEAN) {
        return json_builder_append_cstr(b, v.as.boolean ? "true" : "false") ? 1 : -1;
    }
    if (v.type == PS_T_NUMBER) {
        if (isnan(v.as.number) || isinf(v.as.number)) {
            return json_builder_append_cstr(b, "null") ? 1 : -1;
        }
        char buf[64];
        if (v.as.number == 0.0) {
            snprintf(buf, sizeof(buf), "0");
        } else {
            snprintf(buf, sizeof(buf), "%.15g", v.as.number);
        }
        return json_builder_append_cstr(b, buf) ? 1 : -1;
    }
    if (v.type == PS_T_STRING) {
        return json_stringify_string(b, v.as.string) ? 1 : -1;
    }
    if (v.type == PS_T_OBJECT) {
        PSObject *obj = v.as.object;
        if (!obj) return json_builder_append_cstr(b, "null") ? 1 : -1;
        if (obj->kind == PS_OBJ_KIND_FUNCTION) return 0;
        if ((obj->kind == PS_OBJ_KIND_STRING ||
             obj->kind == PS_OBJ_KIND_NUMBER ||
             obj->kind == PS_OBJ_KIND_BOOLEAN) && obj->internal) {
            PSValue *inner = (PSValue *)obj->internal;
            return json_stringify_value(vm, *inner, b, stack);
        }
        if (!json_stack_push(vm, stack, obj)) return -1;
        int rc = (obj->kind == PS_OBJ_KIND_ARRAY)
            ? json_stringify_array(vm, obj, b, stack)
            : json_stringify_object(vm, obj, b, stack);
        json_stack_pop(stack);
        return rc;
    }
    return 0;
}

static PSValue ps_native_json_parse(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    PSString *input = ps_string_from_cstr("undefined");
    if (argc > 0) {
        input = ps_to_string(vm, argv[0]);
    }
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    PSJsonParser p;
    p.vm = vm;
    p.buf = input ? input->utf8 : "";
    p.len = input ? input->byte_len : 0;
    p.pos = 0;
    p.error = 0;
    PSValue result = json_parse_value(&p);
    if (!p.error) {
        json_skip_ws(&p);
        if (p.pos != p.len) {
            json_parse_error(&p, "Invalid JSON");
        }
    }
    if (p.error) return ps_value_undefined();
    return result;
}

static PSValue ps_native_json_stringify(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (argc == 0) return ps_value_undefined();
    PSJsonBuilder b;
    json_builder_init(&b);
    PSJsonStack stack;
    stack.items = NULL;
    stack.len = 0;
    stack.cap = 0;
    int rc = json_stringify_value(vm, argv[0], &b, &stack);
    json_stack_free(&stack);
    if (rc <= 0 || (vm && vm->has_pending_throw)) {
        json_builder_free(&b);
        return ps_value_undefined();
    }
    PSString *out = (b.len == 0)
        ? ps_string_from_cstr("")
        : ps_string_from_utf8(b.data, b.len);
    json_builder_free(&b);
    return ps_value_string(out);
}

static PSValue ps_native_gc_collect(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    if (vm) {
        ps_gc_collect(vm);
    }
    return ps_value_undefined();
}

static PSValue ps_native_gc_stats(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    if (!vm) return ps_value_undefined();
    PSObject *obj = ps_object_new(vm->object_proto);
    if (!obj) return ps_value_undefined();
    ps_object_define(obj, ps_string_from_cstr("totalBytes"),
                     ps_value_number((double)vm->gc.heap_bytes),
                     PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
    ps_object_define(obj, ps_string_from_cstr("liveBytes"),
                     ps_value_number((double)vm->gc.live_bytes_last),
                     PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
    ps_object_define(obj, ps_string_from_cstr("collections"),
                     ps_value_number((double)vm->gc.collections),
                     PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
    ps_object_define(obj, ps_string_from_cstr("freedLast"),
                     ps_value_number((double)vm->gc.freed_last),
                     PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
    ps_object_define(obj, ps_string_from_cstr("threshold"),
                     ps_value_number((double)vm->gc.threshold),
                     PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
    return ps_value_object(obj);
}

/* --------------------------------------------------------- */
/* VM lifecycle                                              */
/* --------------------------------------------------------- */

PSVM *ps_vm_new(void) {
    PSVM *vm = (PSVM *)calloc(1, sizeof(PSVM));
    if (!vm) return NULL;

    ps_gc_init(vm);
    ps_gc_set_active_vm(vm);

    vm->event_capacity = PS_EVENT_QUEUE_CAPACITY;
    vm->event_queue = (PSValue *)calloc(vm->event_capacity, sizeof(PSValue));
    if (!vm->event_queue) {
        ps_gc_destroy(vm);
        if (ps_gc_active_vm() == vm) {
            ps_gc_set_active_vm(NULL);
        }
        free(vm);
        return NULL;
    }
#if PS_ENABLE_MODULE_DISPLAY
    vm->display = (struct PSDisplay *)calloc(1, sizeof(struct PSDisplay));
    if (!vm->display) {
        free(vm->event_queue);
        ps_gc_destroy(vm);
        if (ps_gc_active_vm() == vm) {
            ps_gc_set_active_vm(NULL);
        }
        free(vm);
        return NULL;
    }
#else
    vm->display = NULL;
#endif
    vm->perf_dump_interval_ms = 0;
    vm->perf_dump_next_ms = 0;
    vm->intern_cache_size = 2048;
    vm->intern_cache = (PSString **)calloc(vm->intern_cache_size, sizeof(PSString *));

    /* Create Global Object (no prototype for now) */
    vm->global = ps_object_new(NULL);
    if (!vm->global) {
        free(vm->event_queue);
        ps_gc_destroy(vm);
        free(vm);
        return NULL;
    }

    vm->env = ps_env_new(NULL, vm->global, 0);
    if (!vm->env) {
        ps_object_free(vm->global);
        free(vm->event_queue);
        ps_gc_destroy(vm);
        if (ps_gc_active_vm() == vm) {
            ps_gc_set_active_vm(NULL);
        }
        free(vm);
        return NULL;
    }

    vm->has_pending_throw = 0;
    vm->pending_throw = ps_value_undefined();
    vm->current_callee = NULL;
    vm->is_constructing = 0;
    vm->root_ast = NULL;
    vm->current_ast = NULL;
    vm->current_node = NULL;
    vm->index_cache_size = 10000;
    vm->index_cache = (PSString **)calloc(vm->index_cache_size, sizeof(PSString *));
    vm->math_intrinsics_valid = 0;
    vm->profile.enabled = 0;
    vm->profile.items = NULL;
    vm->profile.count = 0;
    vm->profile.cap = 0;
    {
        const char *prof_env = getenv("PS_PROFILE_CALLS");
        if (prof_env && prof_env[0]) {
            vm->profile.enabled = 1;
        }
    }
    /* Initialize built-ins and host extensions */
    ps_vm_init_builtins(vm);
    ps_vm_init_buffer(vm);
    ps_vm_init_event(vm);
#if PS_ENABLE_MODULE_DISPLAY
    ps_vm_init_display(vm);
#endif
    ps_vm_init_io(vm);
#if PS_ENABLE_MODULE_FS
    ps_vm_init_fs(vm);
#endif
#if PS_ENABLE_MODULE_IMG
    ps_vm_init_img(vm);
#endif

    return vm;
}

void ps_vm_set_perf_interval(PSVM *vm, uint64_t interval_ms) {
    if (!vm) return;
    if (interval_ms == 0) {
        vm->perf_dump_interval_ms = 0;
        vm->perf_dump_next_ms = 0;
        return;
    }
    vm->perf_dump_interval_ms = interval_ms;
    vm->perf_dump_next_ms = ps_vm_now_ms() + interval_ms;
}

void ps_vm_perf_poll(PSVM *vm) {
    if (!vm || vm->perf_dump_interval_ms == 0) return;
    uint64_t now = ps_vm_now_ms();
    if (now < vm->perf_dump_next_ms) return;
    ps_vm_perf_dump(vm);
    vm->perf_dump_next_ms = now + vm->perf_dump_interval_ms;
}

void ps_vm_perf_dump(PSVM *vm) {
    if (!vm) return;
    fprintf(stderr,
            "perfStats allocCount=%llu allocBytes=%llu objectNew=%llu stringNew=%llu "
            "functionNew=%llu envNew=%llu callCount=%llu evalNodeCount=%llu evalExprCount=%llu "
            "callIdentCount=%llu callMemberCount=%llu callOtherCount=%llu nativeCallCount=%llu "
            "objectGet=%llu objectPut=%llu objectDefine=%llu objectDelete=%llu "
            "arrayGet=%llu arraySet=%llu arrayDelete=%llu stringFromCstr=%llu "
            "bufferReadIndex=%llu bufferWriteIndex=%llu bufferReadIndexFast=%llu bufferWriteIndexFast=%llu "
            "buffer32ReadIndex=%llu buffer32WriteIndex=%llu buffer32ReadIndexFast=%llu buffer32WriteIndexFast=%llu "
            "gcCollections=%d gcLiveBytes=%zu\n",
            (unsigned long long)vm->perf.alloc_count,
            (unsigned long long)vm->perf.alloc_bytes,
            (unsigned long long)vm->perf.object_new,
            (unsigned long long)vm->perf.string_new,
            (unsigned long long)vm->perf.function_new,
            (unsigned long long)vm->perf.env_new,
            (unsigned long long)vm->perf.call_count,
            (unsigned long long)vm->perf.eval_node_count,
            (unsigned long long)vm->perf.eval_expr_count,
            (unsigned long long)vm->perf.call_ident_count,
            (unsigned long long)vm->perf.call_member_count,
            (unsigned long long)vm->perf.call_other_count,
            (unsigned long long)vm->perf.native_call_count,
            (unsigned long long)vm->perf.object_get,
            (unsigned long long)vm->perf.object_put,
            (unsigned long long)vm->perf.object_define,
            (unsigned long long)vm->perf.object_delete,
            (unsigned long long)vm->perf.array_get,
            (unsigned long long)vm->perf.array_set,
            (unsigned long long)vm->perf.array_delete,
            (unsigned long long)vm->perf.string_from_cstr,
            (unsigned long long)vm->perf.buffer_read_index,
            (unsigned long long)vm->perf.buffer_write_index,
            (unsigned long long)vm->perf.buffer_read_index_fast,
            (unsigned long long)vm->perf.buffer_write_index_fast,
            (unsigned long long)vm->perf.buffer32_read_index,
            (unsigned long long)vm->perf.buffer32_write_index,
            (unsigned long long)vm->perf.buffer32_read_index_fast,
            (unsigned long long)vm->perf.buffer32_write_index_fast,
            vm->gc.collections,
            vm->gc.live_bytes_last);
    fprintf(stderr, "perfAstCounts");
    for (size_t i = 0; i < PS_AST_KIND_COUNT; i++) {
        fprintf(stderr, " k%zu=%llu", i, (unsigned long long)vm->perf.ast_counts[i]);
    }
    fprintf(stderr, "\n");
}

void ps_vm_free(PSVM *vm) {
    if (!vm) return;
    if (vm->profile.enabled && vm->profile.count > 0) {
        ps_vm_profile_dump(vm);
    }
    ps_gc_destroy(vm);
    if (ps_gc_active_vm() == vm) {
        ps_gc_set_active_vm(NULL);
    }
#if PS_ENABLE_MODULE_DISPLAY
    ps_display_shutdown(vm);
#endif
    free(vm->event_queue);
    free(vm->index_cache);
    free(vm->intern_cache);
    free(vm->stack_frames);
    free(vm->profile.items);
    free(vm);
}

static int ps_profile_entry_cmp(const void *a, const void *b) {
    const PSProfileEntry *ea = (const PSProfileEntry *)a;
    const PSProfileEntry *eb = (const PSProfileEntry *)b;
    if (ea->total_ms < eb->total_ms) return 1;
    if (ea->total_ms > eb->total_ms) return -1;
    return 0;
}

static void ps_vm_profile_dump(PSVM *vm) {
    if (!vm || !vm->profile.enabled || vm->profile.count == 0) return;
    qsort(vm->profile.items, vm->profile.count, sizeof(PSProfileEntry), ps_profile_entry_cmp);
    size_t limit = vm->profile.count < 32 ? vm->profile.count : 32;
    fprintf(stderr, "profileCalls top=%zu\n", limit);
    for (size_t i = 0; i < limit; i++) {
        PSProfileEntry *e = &vm->profile.items[i];
        const char *name_fallback = "<anon>";
        PSString *name = e->func ? e->func->name : NULL;
        fprintf(stderr, "profileCalls calls=%llu ms=%llu name=",
                (unsigned long long)e->calls,
                (unsigned long long)e->total_ms);
        if (name && name->utf8 && name->byte_len > 0) {
            fwrite(name->utf8, 1, name->byte_len, stderr);
        } else {
            fputs(name_fallback, stderr);
        }
        fprintf(stderr, " func=%p\n", (void *)e->func);
    }
}

void ps_vm_profile_add(PSVM *vm, PSFunction *func, uint64_t elapsed_ms) {
    if (!vm || !vm->profile.enabled || !func) return;
    for (size_t i = 0; i < vm->profile.count; i++) {
        PSProfileEntry *e = &vm->profile.items[i];
        if (e->func == func) {
            e->calls++;
            e->total_ms += elapsed_ms;
            return;
        }
    }
    if (vm->profile.count == vm->profile.cap) {
        size_t new_cap = vm->profile.cap ? vm->profile.cap * 2 : 32;
        PSProfileEntry *next = (PSProfileEntry *)realloc(vm->profile.items,
                                                         new_cap * sizeof(PSProfileEntry));
        if (!next) return;
        vm->profile.items = next;
        vm->profile.cap = new_cap;
    }
    PSProfileEntry *slot = &vm->profile.items[vm->profile.count++];
    slot->func = func;
    slot->calls = 1;
    slot->total_ms = elapsed_ms;
}

/* --------------------------------------------------------- */
/* Accessors                                                 */
/* --------------------------------------------------------- */

PSObject *ps_vm_global(PSVM *vm) {
    return vm ? vm->global : NULL;
}

/* --------------------------------------------------------- */
/* Initialization helpers                                    */
/* --------------------------------------------------------- */

PSObject *ps_vm_wrap_primitive(PSVM *vm, const PSValue *v) {
    if (!vm || !v) return NULL;
    PSObject *proto = vm->object_proto;
    int kind = PS_OBJ_KIND_PLAIN;

    switch (v->type) {
        case PS_T_BOOLEAN:
            proto = vm->boolean_proto ? vm->boolean_proto : vm->object_proto;
            kind = PS_OBJ_KIND_BOOLEAN;
            break;
        case PS_T_NUMBER:
            proto = vm->number_proto ? vm->number_proto : vm->object_proto;
            kind = PS_OBJ_KIND_NUMBER;
            break;
        case PS_T_STRING:
            proto = vm->string_proto ? vm->string_proto : vm->object_proto;
            kind = PS_OBJ_KIND_STRING;
            break;
        default:
            return NULL;
    }

    PSObject *obj = ps_object_new(proto);
    if (!obj) return NULL;
    obj->kind = kind;
    obj->internal = malloc(sizeof(PSValue));
    if (obj->internal) {
        *((PSValue *)obj->internal) = *v;
    }
    if (kind == PS_OBJ_KIND_STRING && v->type == PS_T_STRING) {
        PSString *s = v->as.string;
        ps_object_define(obj,
                         ps_string_from_cstr("length"),
                         ps_value_number((double)(s ? s->glyph_count : 0)),
                         PS_ATTR_READONLY | PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
    }
    return obj;
}

void ps_vm_init_builtins(PSVM *vm) {
    if (!vm) return;

    vm->object_proto = ps_object_new(NULL);
    vm->function_proto = ps_function_new_native(ps_native_empty);
    if (vm->function_proto) {
        vm->function_proto->prototype = vm->object_proto;
        ps_object_define(vm->function_proto,
                         ps_string_from_cstr("length"),
                         ps_value_number(0.0),
                         PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
    }

    vm->boolean_proto = ps_object_new(vm->object_proto);
    vm->number_proto = ps_object_new(vm->object_proto);
    vm->string_proto = ps_object_new(vm->object_proto);
    vm->array_proto = ps_object_new(vm->object_proto);
    vm->date_proto = ps_object_new(vm->object_proto);
    vm->regexp_proto = ps_object_new(vm->object_proto);
    vm->math_obj = ps_object_new(vm->object_proto);
    vm->error_proto = ps_object_new(vm->object_proto);
    vm->type_error_proto = ps_object_new(vm->error_proto);
    vm->range_error_proto = ps_object_new(vm->error_proto);
    vm->reference_error_proto = ps_object_new(vm->error_proto);
    vm->syntax_error_proto = ps_object_new(vm->error_proto);
    vm->eval_error_proto = ps_object_new(vm->error_proto);

    if (vm->global && vm->object_proto) {
        vm->global->prototype = vm->object_proto;
    }

    if (vm->global) {
        ps_object_define(vm->global,
                         ps_string_from_cstr("undefined"),
                         ps_value_undefined(),
                         PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
        ps_object_define(vm->global,
                         ps_string_from_cstr("NaN"),
                         ps_value_number(NAN),
                         PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
    }

    PSObject *is_finite_fn = ps_function_new_native(ps_native_is_finite);
    if (is_finite_fn) {
        ps_function_setup(is_finite_fn, vm->function_proto, vm->object_proto, NULL);
        ps_define_function_props(is_finite_fn, "isFinite", 1);
        ps_object_define(vm->global,
                         ps_string_from_cstr("isFinite"),
                         ps_value_object(is_finite_fn),
                         PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
    }

    PSObject *is_nan_fn = ps_function_new_native(ps_native_is_nan);
    if (is_nan_fn) {
        ps_function_setup(is_nan_fn, vm->function_proto, vm->object_proto, NULL);
        ps_define_function_props(is_nan_fn, "isNaN", 1);
        ps_object_define(vm->global,
                         ps_string_from_cstr("isNaN"),
                         ps_value_object(is_nan_fn),
                         PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
    }

    PSObject *parse_int_fn = ps_function_new_native(ps_native_parse_int);
    if (parse_int_fn) {
        ps_function_setup(parse_int_fn, vm->function_proto, vm->object_proto, NULL);
        ps_define_function_props(parse_int_fn, "parseInt", 2);
        ps_object_define(vm->global,
                         ps_string_from_cstr("parseInt"),
                         ps_value_object(parse_int_fn),
                         PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
    }

    PSObject *parse_float_fn = ps_function_new_native(ps_native_parse_float);
    if (parse_float_fn) {
        ps_function_setup(parse_float_fn, vm->function_proto, vm->object_proto, NULL);
        ps_define_function_props(parse_float_fn, "parseFloat", 1);
        ps_object_define(vm->global,
                         ps_string_from_cstr("parseFloat"),
                         ps_value_object(parse_float_fn),
                         PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
    }

    PSObject *escape_fn = ps_function_new_native(ps_native_escape);
    if (escape_fn) {
        ps_function_setup(escape_fn, vm->function_proto, vm->object_proto, NULL);
        ps_define_function_props(escape_fn, "escape", 1);
        ps_object_define(vm->global,
                         ps_string_from_cstr("escape"),
                         ps_value_object(escape_fn),
                         PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
    }

    PSObject *unescape_fn = ps_function_new_native(ps_native_unescape);
    if (unescape_fn) {
        ps_function_setup(unescape_fn, vm->function_proto, vm->object_proto, NULL);
        ps_define_function_props(unescape_fn, "unescape", 1);
        ps_object_define(vm->global,
                         ps_string_from_cstr("unescape"),
                         ps_value_object(unescape_fn),
                         PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
    }

    PSObject *error_ctor = ps_function_new_native(ps_native_error);
    if (error_ctor) {
        ps_function_setup(error_ctor, vm->function_proto, vm->object_proto, vm->error_proto);
        ps_define_function_props(error_ctor, "Error", 1);
        if (vm->error_proto) {
            ps_object_define(vm->error_proto,
                             ps_string_from_cstr("constructor"),
                             ps_value_object(error_ctor),
                             PS_ATTR_DONTENUM);
            ps_object_define(vm->error_proto,
                             ps_string_from_cstr("name"),
                             ps_value_string(ps_string_from_cstr("Error")),
                             PS_ATTR_DONTENUM);
            ps_object_define(vm->error_proto,
                             ps_string_from_cstr("message"),
                             ps_value_string(ps_string_from_cstr("")),
                             PS_ATTR_DONTENUM);
            PSObject *to_string_fn = ps_function_new_native(ps_native_error_to_string);
            if (to_string_fn) {
                ps_function_setup(to_string_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->error_proto,
                                 ps_string_from_cstr("toString"),
                                 ps_value_object(to_string_fn),
                                 PS_ATTR_DONTENUM);
            }
        }
        ps_object_define(vm->global,
                         ps_string_from_cstr("Error"),
                         ps_value_object(error_ctor),
                         PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
    }

    PSObject *type_error_ctor = ps_function_new_native(ps_native_type_error);
    if (type_error_ctor) {
        ps_function_setup(type_error_ctor, vm->function_proto, vm->object_proto, vm->type_error_proto);
        ps_define_function_props(type_error_ctor, "TypeError", 1);
        if (vm->type_error_proto) {
            ps_object_define(vm->type_error_proto,
                             ps_string_from_cstr("constructor"),
                             ps_value_object(type_error_ctor),
                             PS_ATTR_DONTENUM);
            ps_object_define(vm->type_error_proto,
                             ps_string_from_cstr("name"),
                             ps_value_string(ps_string_from_cstr("TypeError")),
                             PS_ATTR_DONTENUM);
            ps_object_define(vm->type_error_proto,
                             ps_string_from_cstr("message"),
                             ps_value_string(ps_string_from_cstr("")),
                             PS_ATTR_DONTENUM);
        }
        ps_object_define(vm->global,
                         ps_string_from_cstr("TypeError"),
                         ps_value_object(type_error_ctor),
                         PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
    }

    PSObject *range_error_ctor = ps_function_new_native(ps_native_range_error);
    if (range_error_ctor) {
        ps_function_setup(range_error_ctor, vm->function_proto, vm->object_proto, vm->range_error_proto);
        ps_define_function_props(range_error_ctor, "RangeError", 1);
        if (vm->range_error_proto) {
            ps_object_define(vm->range_error_proto,
                             ps_string_from_cstr("constructor"),
                             ps_value_object(range_error_ctor),
                             PS_ATTR_DONTENUM);
            ps_object_define(vm->range_error_proto,
                             ps_string_from_cstr("name"),
                             ps_value_string(ps_string_from_cstr("RangeError")),
                             PS_ATTR_DONTENUM);
            ps_object_define(vm->range_error_proto,
                             ps_string_from_cstr("message"),
                             ps_value_string(ps_string_from_cstr("")),
                             PS_ATTR_DONTENUM);
        }
        ps_object_define(vm->global,
                         ps_string_from_cstr("RangeError"),
                         ps_value_object(range_error_ctor),
                         PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
    }

    PSObject *reference_error_ctor = ps_function_new_native(ps_native_reference_error);
    if (reference_error_ctor) {
        ps_function_setup(reference_error_ctor, vm->function_proto, vm->object_proto, vm->reference_error_proto);
        ps_define_function_props(reference_error_ctor, "ReferenceError", 1);
        if (vm->reference_error_proto) {
            ps_object_define(vm->reference_error_proto,
                             ps_string_from_cstr("constructor"),
                             ps_value_object(reference_error_ctor),
                             PS_ATTR_DONTENUM);
            ps_object_define(vm->reference_error_proto,
                             ps_string_from_cstr("name"),
                             ps_value_string(ps_string_from_cstr("ReferenceError")),
                             PS_ATTR_DONTENUM);
            ps_object_define(vm->reference_error_proto,
                             ps_string_from_cstr("message"),
                             ps_value_string(ps_string_from_cstr("")),
                             PS_ATTR_DONTENUM);
        }
        ps_object_define(vm->global,
                         ps_string_from_cstr("ReferenceError"),
                         ps_value_object(reference_error_ctor),
                         PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
    }

    PSObject *syntax_error_ctor = ps_function_new_native(ps_native_syntax_error);
    if (syntax_error_ctor) {
        ps_function_setup(syntax_error_ctor, vm->function_proto, vm->object_proto, vm->syntax_error_proto);
        ps_define_function_props(syntax_error_ctor, "SyntaxError", 1);
        if (vm->syntax_error_proto) {
            ps_object_define(vm->syntax_error_proto,
                             ps_string_from_cstr("constructor"),
                             ps_value_object(syntax_error_ctor),
                             PS_ATTR_DONTENUM);
            ps_object_define(vm->syntax_error_proto,
                             ps_string_from_cstr("name"),
                             ps_value_string(ps_string_from_cstr("SyntaxError")),
                             PS_ATTR_DONTENUM);
            ps_object_define(vm->syntax_error_proto,
                             ps_string_from_cstr("message"),
                             ps_value_string(ps_string_from_cstr("")),
                             PS_ATTR_DONTENUM);
        }
        ps_object_define(vm->global,
                         ps_string_from_cstr("SyntaxError"),
                         ps_value_object(syntax_error_ctor),
                         PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
    }

    PSObject *eval_error_ctor = ps_function_new_native(ps_native_eval_error);
    if (eval_error_ctor) {
        ps_function_setup(eval_error_ctor, vm->function_proto, vm->object_proto, vm->eval_error_proto);
        ps_define_function_props(eval_error_ctor, "EvalError", 1);
        if (vm->eval_error_proto) {
            ps_object_define(vm->eval_error_proto,
                             ps_string_from_cstr("constructor"),
                             ps_value_object(eval_error_ctor),
                             PS_ATTR_DONTENUM);
            ps_object_define(vm->eval_error_proto,
                             ps_string_from_cstr("name"),
                             ps_value_string(ps_string_from_cstr("EvalError")),
                             PS_ATTR_DONTENUM);
            ps_object_define(vm->eval_error_proto,
                             ps_string_from_cstr("message"),
                             ps_value_string(ps_string_from_cstr("")),
                             PS_ATTR_DONTENUM);
        }
        ps_object_define(vm->global,
                         ps_string_from_cstr("EvalError"),
                         ps_value_object(eval_error_ctor),
                         PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
    }

    PSObject *object_ctor = ps_function_new_native(ps_native_object);
    if (object_ctor) {
        ps_function_setup(object_ctor, vm->function_proto, vm->object_proto, vm->object_proto);
        ps_define_function_props(object_ctor, "Object", 1);
        PSObject *get_proto_fn = ps_function_new_native(ps_native_object_get_prototype_of);
        if (get_proto_fn) {
            ps_function_setup(get_proto_fn, vm->function_proto, vm->object_proto, NULL);
            ps_define_function_props(get_proto_fn, "getPrototypeOf", 1);
            ps_object_define(object_ctor,
                             ps_string_from_cstr("getPrototypeOf"),
                             ps_value_object(get_proto_fn),
                             PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
        }
        PSObject *set_proto_fn = ps_function_new_native(ps_native_object_set_prototype_of);
        if (set_proto_fn) {
            ps_function_setup(set_proto_fn, vm->function_proto, vm->object_proto, NULL);
            ps_define_function_props(set_proto_fn, "setPrototypeOf", 2);
            ps_object_define(object_ctor,
                             ps_string_from_cstr("setPrototypeOf"),
                             ps_value_object(set_proto_fn),
                             PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
        }
        PSObject *create_fn = ps_function_new_native(ps_native_object_create);
        if (create_fn) {
            ps_function_setup(create_fn, vm->function_proto, vm->object_proto, NULL);
            ps_define_function_props(create_fn, "create", 1);
            ps_object_define(object_ctor,
                             ps_string_from_cstr("create"),
                             ps_value_object(create_fn),
                             PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
        }
        if (vm->object_proto) {
            ps_object_define(vm->object_proto,
                         ps_string_from_cstr("constructor"),
                         ps_value_object(object_ctor),
                         PS_ATTR_DONTENUM);
            PSObject *to_string_fn = ps_function_new_native(ps_native_object_to_string);
            if (to_string_fn) {
                ps_function_setup(to_string_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->object_proto,
                             ps_string_from_cstr("toString"),
                             ps_value_object(to_string_fn),
                             PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *to_locale_fn = ps_function_new_native(ps_native_object_to_locale_string);
            if (to_locale_fn) {
                ps_function_setup(to_locale_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->object_proto,
                             ps_string_from_cstr("toLocaleString"),
                             ps_value_object(to_locale_fn),
                             PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *has_own_fn = ps_function_new_native(ps_native_has_own_property);
            if (has_own_fn) {
                ps_function_setup(has_own_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->object_proto,
                             ps_string_from_cstr("hasOwnProperty"),
                             ps_value_object(has_own_fn),
                             PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *prop_enum_fn = ps_function_new_native(ps_native_object_property_is_enumerable);
            if (prop_enum_fn) {
                ps_function_setup(prop_enum_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->object_proto,
                             ps_string_from_cstr("propertyIsEnumerable"),
                             ps_value_object(prop_enum_fn),
                             PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *is_proto_fn = ps_function_new_native(ps_native_object_is_prototype_of);
            if (is_proto_fn) {
                ps_function_setup(is_proto_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->object_proto,
                             ps_string_from_cstr("isPrototypeOf"),
                             ps_value_object(is_proto_fn),
                             PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *value_of_fn = ps_function_new_native(ps_native_object_value_of);
            if (value_of_fn) {
                ps_function_setup(value_of_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->object_proto,
                             ps_string_from_cstr("valueOf"),
                             ps_value_object(value_of_fn),
                             PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
        }
        ps_object_define(vm->global,
                         ps_string_from_cstr("Object"),
                         ps_value_object(object_ctor),
                         PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
    }

    PSObject *function_ctor = ps_function_new_native(ps_native_function);
    if (function_ctor) {
        ps_function_setup(function_ctor, vm->function_proto, vm->object_proto, vm->function_proto);
        ps_define_function_props(function_ctor, "Function", 1);
        if (vm->function_proto) {
            ps_object_define(vm->function_proto,
                             ps_string_from_cstr("constructor"),
                             ps_value_object(function_ctor),
                             PS_ATTR_DONTENUM);
            PSObject *call_fn = ps_function_new_native(ps_native_function_call);
            if (call_fn) {
                ps_function_setup(call_fn, vm->function_proto, vm->object_proto, NULL);
                ps_define_function_props(call_fn, "call", 1);
                ps_object_define(vm->function_proto,
                                 ps_string_from_cstr("call"),
                                 ps_value_object(call_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *apply_fn = ps_function_new_native(ps_native_function_apply);
            if (apply_fn) {
                ps_function_setup(apply_fn, vm->function_proto, vm->object_proto, NULL);
                ps_define_function_props(apply_fn, "apply", 2);
                ps_object_define(vm->function_proto,
                                 ps_string_from_cstr("apply"),
                                 ps_value_object(apply_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *bind_fn = ps_function_new_native(ps_native_function_bind);
            if (bind_fn) {
                ps_function_setup(bind_fn, vm->function_proto, vm->object_proto, NULL);
                ps_define_function_props(bind_fn, "bind", 1);
                ps_object_define(vm->function_proto,
                                 ps_string_from_cstr("bind"),
                                 ps_value_object(bind_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *to_string_fn = ps_function_new_native(ps_native_function_to_string);
            if (to_string_fn) {
                ps_function_setup(to_string_fn, vm->function_proto, vm->object_proto, NULL);
                ps_define_function_props(to_string_fn, "toString", 0);
                ps_object_define(vm->function_proto,
                                 ps_string_from_cstr("toString"),
                                 ps_value_object(to_string_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *value_of_fn = ps_function_new_native(ps_native_function_value_of);
            if (value_of_fn) {
                ps_function_setup(value_of_fn, vm->function_proto, vm->object_proto, NULL);
                ps_define_function_props(value_of_fn, "valueOf", 0);
                ps_object_define(vm->function_proto,
                                 ps_string_from_cstr("valueOf"),
                                 ps_value_object(value_of_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
        }
        ps_object_define(vm->global,
                         ps_string_from_cstr("Function"),
                         ps_value_object(function_ctor),
                         PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
    }

    PSObject *boolean_ctor = ps_function_new_native(ps_native_boolean);
    if (boolean_ctor) {
        ps_function_setup(boolean_ctor, vm->function_proto, vm->object_proto, vm->boolean_proto);
        ps_define_function_props(boolean_ctor, "Boolean", 1);
        if (vm->boolean_proto) {
            ps_object_define(vm->boolean_proto,
                             ps_string_from_cstr("constructor"),
                             ps_value_object(boolean_ctor),
                             PS_ATTR_DONTENUM);
            PSObject *to_string_fn = ps_function_new_native(ps_native_boolean_to_string);
            if (to_string_fn) {
                ps_function_setup(to_string_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->boolean_proto,
                                 ps_string_from_cstr("toString"),
                                 ps_value_object(to_string_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *value_of_fn = ps_function_new_native(ps_native_boolean_value_of);
            if (value_of_fn) {
                ps_function_setup(value_of_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->boolean_proto,
                                 ps_string_from_cstr("valueOf"),
                                 ps_value_object(value_of_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
        }
        ps_object_define(vm->global,
                         ps_string_from_cstr("Boolean"),
                         ps_value_object(boolean_ctor),
                         PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
    }

    PSObject *number_ctor = ps_function_new_native(ps_native_number);
    if (number_ctor) {
        ps_function_setup(number_ctor, vm->function_proto, vm->object_proto, vm->number_proto);
        ps_define_function_props(number_ctor, "Number", 1);
        if (vm->number_proto) {
            ps_object_define(vm->number_proto,
                             ps_string_from_cstr("constructor"),
                             ps_value_object(number_ctor),
                             PS_ATTR_DONTENUM);
            PSObject *to_string_fn = ps_function_new_native(ps_native_number_to_string);
            if (to_string_fn) {
                ps_function_setup(to_string_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->number_proto,
                                 ps_string_from_cstr("toString"),
                                 ps_value_object(to_string_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *value_of_fn = ps_function_new_native(ps_native_number_value_of);
            if (value_of_fn) {
                ps_function_setup(value_of_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->number_proto,
                                 ps_string_from_cstr("valueOf"),
                                 ps_value_object(value_of_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *to_fixed_fn = ps_function_new_native(ps_native_number_to_fixed);
            if (to_fixed_fn) {
                ps_function_setup(to_fixed_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->number_proto,
                                 ps_string_from_cstr("toFixed"),
                                 ps_value_object(to_fixed_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *to_exp_fn = ps_function_new_native(ps_native_number_to_exponential);
            if (to_exp_fn) {
                ps_function_setup(to_exp_fn, vm->function_proto, vm->object_proto, NULL);
                ps_define_function_props(to_exp_fn, "toExponential", 1);
                ps_object_define(vm->number_proto,
                                 ps_string_from_cstr("toExponential"),
                                 ps_value_object(to_exp_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *to_prec_fn = ps_function_new_native(ps_native_number_to_precision);
            if (to_prec_fn) {
                ps_function_setup(to_prec_fn, vm->function_proto, vm->object_proto, NULL);
                ps_define_function_props(to_prec_fn, "toPrecision", 1);
                ps_object_define(vm->number_proto,
                                 ps_string_from_cstr("toPrecision"),
                                 ps_value_object(to_prec_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
        }
        ps_object_define(vm->global,
                         ps_string_from_cstr("Number"),
                         ps_value_object(number_ctor),
                         PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
    }

    PSObject *string_ctor = ps_function_new_native(ps_native_string);
    if (string_ctor) {
        ps_function_setup(string_ctor, vm->function_proto, vm->object_proto, vm->string_proto);
        ps_define_function_props(string_ctor, "String", 1);
        PSObject *from_char_code_fn = ps_function_new_native(ps_native_string_from_char_code);
        if (from_char_code_fn) {
            ps_function_setup(from_char_code_fn, vm->function_proto, vm->object_proto, NULL);
            ps_define_function_props(from_char_code_fn, "fromCharCode", 1);
            ps_object_define(string_ctor,
                             ps_string_from_cstr("fromCharCode"),
                             ps_value_object(from_char_code_fn),
                             PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
        }
        if (vm->string_proto) {
            ps_object_define(vm->string_proto,
                             ps_string_from_cstr("constructor"),
                             ps_value_object(string_ctor),
                             PS_ATTR_DONTENUM);
            PSObject *to_string_fn = ps_function_new_native(ps_native_string_to_string);
            if (to_string_fn) {
                ps_function_setup(to_string_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->string_proto,
                                 ps_string_from_cstr("toString"),
                                 ps_value_object(to_string_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *value_of_fn = ps_function_new_native(ps_native_string_value_of);
            if (value_of_fn) {
                ps_function_setup(value_of_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->string_proto,
                                 ps_string_from_cstr("valueOf"),
                                 ps_value_object(value_of_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *char_at_fn = ps_function_new_native(ps_native_string_char_at);
            if (char_at_fn) {
                ps_function_setup(char_at_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->string_proto,
                                 ps_string_from_cstr("charAt"),
                                 ps_value_object(char_at_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *char_code_fn = ps_function_new_native(ps_native_string_char_code_at);
            if (char_code_fn) {
                ps_function_setup(char_code_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->string_proto,
                                 ps_string_from_cstr("charCodeAt"),
                                 ps_value_object(char_code_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *index_of_fn = ps_function_new_native(ps_native_string_index_of);
            if (index_of_fn) {
                ps_function_setup(index_of_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->string_proto,
                                 ps_string_from_cstr("indexOf"),
                                 ps_value_object(index_of_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *substring_fn = ps_function_new_native(ps_native_string_substring);
            if (substring_fn) {
                ps_function_setup(substring_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->string_proto,
                                 ps_string_from_cstr("substring"),
                                 ps_value_object(substring_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *slice_fn = ps_function_new_native(ps_native_string_slice);
            if (slice_fn) {
                ps_function_setup(slice_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->string_proto,
                                 ps_string_from_cstr("slice"),
                                 ps_value_object(slice_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *concat_fn = ps_function_new_native(ps_native_string_concat);
            if (concat_fn) {
                ps_function_setup(concat_fn, vm->function_proto, vm->object_proto, NULL);
                ps_define_function_props(concat_fn, "concat", 1);
                ps_object_define(vm->string_proto,
                                 ps_string_from_cstr("concat"),
                                 ps_value_object(concat_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *last_index_fn = ps_function_new_native(ps_native_string_last_index_of);
            if (last_index_fn) {
                ps_function_setup(last_index_fn, vm->function_proto, vm->object_proto, NULL);
                ps_define_function_props(last_index_fn, "lastIndexOf", 1);
                ps_object_define(vm->string_proto,
                                 ps_string_from_cstr("lastIndexOf"),
                                 ps_value_object(last_index_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *split_fn = ps_function_new_native(ps_native_string_split);
            if (split_fn) {
                ps_function_setup(split_fn, vm->function_proto, vm->object_proto, NULL);
                ps_define_function_props(split_fn, "split", 2);
                ps_object_define(vm->string_proto,
                                 ps_string_from_cstr("split"),
                                 ps_value_object(split_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *replace_fn = ps_function_new_native(ps_native_string_replace);
            if (replace_fn) {
                ps_function_setup(replace_fn, vm->function_proto, vm->object_proto, NULL);
                ps_define_function_props(replace_fn, "replace", 2);
                ps_object_define(vm->string_proto,
                                 ps_string_from_cstr("replace"),
                                 ps_value_object(replace_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *match_fn = ps_function_new_native(ps_native_string_match);
            if (match_fn) {
                ps_function_setup(match_fn, vm->function_proto, vm->object_proto, NULL);
                ps_define_function_props(match_fn, "match", 1);
                ps_object_define(vm->string_proto,
                                 ps_string_from_cstr("match"),
                                 ps_value_object(match_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *search_fn = ps_function_new_native(ps_native_string_search);
            if (search_fn) {
                ps_function_setup(search_fn, vm->function_proto, vm->object_proto, NULL);
                ps_define_function_props(search_fn, "search", 1);
                ps_object_define(vm->string_proto,
                                 ps_string_from_cstr("search"),
                                 ps_value_object(search_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
        }
        ps_object_define(vm->global,
                         ps_string_from_cstr("String"),
                         ps_value_object(string_ctor),
                         PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
    }

    PSObject *array_ctor = ps_function_new_native(ps_native_array);
    if (array_ctor) {
        ps_function_setup(array_ctor, vm->function_proto, vm->object_proto, vm->array_proto);
        ps_define_function_props(array_ctor, "Array", 1);
        if (vm->array_proto) {
            ps_object_define(vm->array_proto,
                             ps_string_from_cstr("constructor"),
                             ps_value_object(array_ctor),
                             PS_ATTR_DONTENUM);
            PSObject *to_string_fn = ps_function_new_native(ps_native_array_to_string);
            if (to_string_fn) {
                ps_function_setup(to_string_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->array_proto,
                                 ps_string_from_cstr("toString"),
                                 ps_value_object(to_string_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *join_fn = ps_function_new_native(ps_native_array_join);
            if (join_fn) {
                ps_function_setup(join_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->array_proto,
                                 ps_string_from_cstr("join"),
                                 ps_value_object(join_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *push_fn = ps_function_new_native(ps_native_array_push);
            if (push_fn) {
                ps_function_setup(push_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->array_proto,
                                 ps_string_from_cstr("push"),
                                 ps_value_object(push_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *pop_fn = ps_function_new_native(ps_native_array_pop);
            if (pop_fn) {
                ps_function_setup(pop_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->array_proto,
                                 ps_string_from_cstr("pop"),
                                 ps_value_object(pop_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *shift_fn = ps_function_new_native(ps_native_array_shift);
            if (shift_fn) {
                ps_function_setup(shift_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->array_proto,
                                 ps_string_from_cstr("shift"),
                                 ps_value_object(shift_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *unshift_fn = ps_function_new_native(ps_native_array_unshift);
            if (unshift_fn) {
                ps_function_setup(unshift_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->array_proto,
                                 ps_string_from_cstr("unshift"),
                                 ps_value_object(unshift_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *slice_fn = ps_function_new_native(ps_native_array_slice);
            if (slice_fn) {
                ps_function_setup(slice_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->array_proto,
                                 ps_string_from_cstr("slice"),
                                 ps_value_object(slice_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *concat_fn = ps_function_new_native(ps_native_array_concat);
            if (concat_fn) {
                ps_function_setup(concat_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->array_proto,
                                 ps_string_from_cstr("concat"),
                                 ps_value_object(concat_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *reverse_fn = ps_function_new_native(ps_native_array_reverse);
            if (reverse_fn) {
                ps_function_setup(reverse_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->array_proto,
                                 ps_string_from_cstr("reverse"),
                                 ps_value_object(reverse_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *sort_fn = ps_function_new_native(ps_native_array_sort);
            if (sort_fn) {
                ps_function_setup(sort_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->array_proto,
                                 ps_string_from_cstr("sort"),
                                 ps_value_object(sort_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *splice_fn = ps_function_new_native(ps_native_array_splice);
            if (splice_fn) {
                ps_function_setup(splice_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->array_proto,
                                 ps_string_from_cstr("splice"),
                                 ps_value_object(splice_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
        }
        ps_object_define(vm->global,
                         ps_string_from_cstr("Array"),
                         ps_value_object(array_ctor),
                         PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
    }

    PSObject *date_ctor = ps_function_new_native(ps_native_date);
    if (date_ctor) {
        ps_function_setup(date_ctor, vm->function_proto, vm->object_proto, vm->date_proto);
        ps_define_function_props(date_ctor, "Date", 7);
        if (vm->date_proto) {
            ps_object_define(vm->date_proto,
                             ps_string_from_cstr("constructor"),
                             ps_value_object(date_ctor),
                             PS_ATTR_DONTENUM);
            PSObject *to_string_fn = ps_function_new_native(ps_native_date_to_string);
            if (to_string_fn) {
                ps_function_setup(to_string_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->date_proto,
                                 ps_string_from_cstr("toString"),
                                 ps_value_object(to_string_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *to_utc_fn = ps_function_new_native(ps_native_date_to_utc_string);
            if (to_utc_fn) {
                ps_function_setup(to_utc_fn, vm->function_proto, vm->object_proto, NULL);
                ps_define_function_props(to_utc_fn, "toUTCString", 0);
                ps_object_define(vm->date_proto,
                                 ps_string_from_cstr("toUTCString"),
                                 ps_value_object(to_utc_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *to_locale_fn = ps_function_new_native(ps_native_date_to_locale_string);
            if (to_locale_fn) {
                ps_function_setup(to_locale_fn, vm->function_proto, vm->object_proto, NULL);
                ps_define_function_props(to_locale_fn, "toLocaleString", 0);
                ps_object_define(vm->date_proto,
                                 ps_string_from_cstr("toLocaleString"),
                                 ps_value_object(to_locale_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *value_of_fn = ps_function_new_native(ps_native_date_value_of);
            if (value_of_fn) {
                ps_function_setup(value_of_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->date_proto,
                                 ps_string_from_cstr("valueOf"),
                                 ps_value_object(value_of_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *get_time_fn = ps_function_new_native(ps_native_date_get_time);
            if (get_time_fn) {
                ps_function_setup(get_time_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->date_proto,
                                 ps_string_from_cstr("getTime"),
                                 ps_value_object(get_time_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *get_tz_fn = ps_function_new_native(ps_native_date_get_timezone_offset);
            if (get_tz_fn) {
                ps_function_setup(get_tz_fn, vm->function_proto, vm->object_proto, NULL);
                ps_define_function_props(get_tz_fn, "getTimezoneOffset", 0);
                ps_object_define(vm->date_proto,
                                 ps_string_from_cstr("getTimezoneOffset"),
                                 ps_value_object(get_tz_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *get_full_year_fn = ps_function_new_native(ps_native_date_get_full_year);
            if (get_full_year_fn) {
                ps_function_setup(get_full_year_fn, vm->function_proto, vm->object_proto, NULL);
                ps_define_function_props(get_full_year_fn, "getFullYear", 0);
                ps_object_define(vm->date_proto,
                                 ps_string_from_cstr("getFullYear"),
                                 ps_value_object(get_full_year_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *get_month_fn = ps_function_new_native(ps_native_date_get_month);
            if (get_month_fn) {
                ps_function_setup(get_month_fn, vm->function_proto, vm->object_proto, NULL);
                ps_define_function_props(get_month_fn, "getMonth", 0);
                ps_object_define(vm->date_proto,
                                 ps_string_from_cstr("getMonth"),
                                 ps_value_object(get_month_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *get_date_fn = ps_function_new_native(ps_native_date_get_date);
            if (get_date_fn) {
                ps_function_setup(get_date_fn, vm->function_proto, vm->object_proto, NULL);
                ps_define_function_props(get_date_fn, "getDate", 0);
                ps_object_define(vm->date_proto,
                                 ps_string_from_cstr("getDate"),
                                 ps_value_object(get_date_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *get_day_fn = ps_function_new_native(ps_native_date_get_day);
            if (get_day_fn) {
                ps_function_setup(get_day_fn, vm->function_proto, vm->object_proto, NULL);
                ps_define_function_props(get_day_fn, "getDay", 0);
                ps_object_define(vm->date_proto,
                                 ps_string_from_cstr("getDay"),
                                 ps_value_object(get_day_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *get_hours_fn = ps_function_new_native(ps_native_date_get_hours);
            if (get_hours_fn) {
                ps_function_setup(get_hours_fn, vm->function_proto, vm->object_proto, NULL);
                ps_define_function_props(get_hours_fn, "getHours", 0);
                ps_object_define(vm->date_proto,
                                 ps_string_from_cstr("getHours"),
                                 ps_value_object(get_hours_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *get_minutes_fn = ps_function_new_native(ps_native_date_get_minutes);
            if (get_minutes_fn) {
                ps_function_setup(get_minutes_fn, vm->function_proto, vm->object_proto, NULL);
                ps_define_function_props(get_minutes_fn, "getMinutes", 0);
                ps_object_define(vm->date_proto,
                                 ps_string_from_cstr("getMinutes"),
                                 ps_value_object(get_minutes_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *get_seconds_fn = ps_function_new_native(ps_native_date_get_seconds);
            if (get_seconds_fn) {
                ps_function_setup(get_seconds_fn, vm->function_proto, vm->object_proto, NULL);
                ps_define_function_props(get_seconds_fn, "getSeconds", 0);
                ps_object_define(vm->date_proto,
                                 ps_string_from_cstr("getSeconds"),
                                 ps_value_object(get_seconds_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *get_ms_fn = ps_function_new_native(ps_native_date_get_milliseconds);
            if (get_ms_fn) {
                ps_function_setup(get_ms_fn, vm->function_proto, vm->object_proto, NULL);
                ps_define_function_props(get_ms_fn, "getMilliseconds", 0);
                ps_object_define(vm->date_proto,
                                 ps_string_from_cstr("getMilliseconds"),
                                 ps_value_object(get_ms_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *set_full_year_fn = ps_function_new_native(ps_native_date_set_full_year);
            if (set_full_year_fn) {
                ps_function_setup(set_full_year_fn, vm->function_proto, vm->object_proto, NULL);
                ps_define_function_props(set_full_year_fn, "setFullYear", 3);
                ps_object_define(vm->date_proto,
                                 ps_string_from_cstr("setFullYear"),
                                 ps_value_object(set_full_year_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *set_month_fn = ps_function_new_native(ps_native_date_set_month);
            if (set_month_fn) {
                ps_function_setup(set_month_fn, vm->function_proto, vm->object_proto, NULL);
                ps_define_function_props(set_month_fn, "setMonth", 2);
                ps_object_define(vm->date_proto,
                                 ps_string_from_cstr("setMonth"),
                                 ps_value_object(set_month_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *set_date_fn = ps_function_new_native(ps_native_date_set_date);
            if (set_date_fn) {
                ps_function_setup(set_date_fn, vm->function_proto, vm->object_proto, NULL);
                ps_define_function_props(set_date_fn, "setDate", 1);
                ps_object_define(vm->date_proto,
                                 ps_string_from_cstr("setDate"),
                                 ps_value_object(set_date_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *set_hours_fn = ps_function_new_native(ps_native_date_set_hours);
            if (set_hours_fn) {
                ps_function_setup(set_hours_fn, vm->function_proto, vm->object_proto, NULL);
                ps_define_function_props(set_hours_fn, "setHours", 4);
                ps_object_define(vm->date_proto,
                                 ps_string_from_cstr("setHours"),
                                 ps_value_object(set_hours_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *set_minutes_fn = ps_function_new_native(ps_native_date_set_minutes);
            if (set_minutes_fn) {
                ps_function_setup(set_minutes_fn, vm->function_proto, vm->object_proto, NULL);
                ps_define_function_props(set_minutes_fn, "setMinutes", 3);
                ps_object_define(vm->date_proto,
                                 ps_string_from_cstr("setMinutes"),
                                 ps_value_object(set_minutes_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *set_seconds_fn = ps_function_new_native(ps_native_date_set_seconds);
            if (set_seconds_fn) {
                ps_function_setup(set_seconds_fn, vm->function_proto, vm->object_proto, NULL);
                ps_define_function_props(set_seconds_fn, "setSeconds", 2);
                ps_object_define(vm->date_proto,
                                 ps_string_from_cstr("setSeconds"),
                                 ps_value_object(set_seconds_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *set_ms_fn = ps_function_new_native(ps_native_date_set_milliseconds);
            if (set_ms_fn) {
                ps_function_setup(set_ms_fn, vm->function_proto, vm->object_proto, NULL);
                ps_define_function_props(set_ms_fn, "setMilliseconds", 1);
                ps_object_define(vm->date_proto,
                                 ps_string_from_cstr("setMilliseconds"),
                                 ps_value_object(set_ms_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
        }
        PSObject *parse_fn = ps_function_new_native(ps_native_date_parse);
        if (parse_fn) {
            ps_function_setup(parse_fn, vm->function_proto, vm->object_proto, NULL);
            ps_object_define(date_ctor,
                             ps_string_from_cstr("parse"),
                             ps_value_object(parse_fn),
                             PS_ATTR_NONE);
        }
        PSObject *utc_fn = ps_function_new_native(ps_native_date_utc);
        if (utc_fn) {
            ps_function_setup(utc_fn, vm->function_proto, vm->object_proto, NULL);
            ps_object_define(date_ctor,
                             ps_string_from_cstr("UTC"),
                             ps_value_object(utc_fn),
                             PS_ATTR_NONE);
        }
        ps_object_define(vm->global,
                         ps_string_from_cstr("Date"),
                         ps_value_object(date_ctor),
                         PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
    }

    PSObject *regexp_ctor = ps_function_new_native(ps_native_regexp);
    if (regexp_ctor) {
        ps_function_setup(regexp_ctor, vm->function_proto, vm->object_proto, vm->regexp_proto);
        ps_define_function_props(regexp_ctor, "RegExp", 2);
        if (vm->regexp_proto) {
            ps_object_define(vm->regexp_proto,
                             ps_string_from_cstr("constructor"),
                             ps_value_object(regexp_ctor),
                             PS_ATTR_DONTENUM);
            PSObject *to_string_fn = ps_function_new_native(ps_native_regexp_to_string);
            if (to_string_fn) {
                ps_function_setup(to_string_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->regexp_proto,
                                 ps_string_from_cstr("toString"),
                                 ps_value_object(to_string_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *exec_fn = ps_function_new_native(ps_native_regexp_exec);
            if (exec_fn) {
                ps_function_setup(exec_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->regexp_proto,
                                 ps_string_from_cstr("exec"),
                                 ps_value_object(exec_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
            PSObject *test_fn = ps_function_new_native(ps_native_regexp_test);
            if (test_fn) {
                ps_function_setup(test_fn, vm->function_proto, vm->object_proto, NULL);
                ps_object_define(vm->regexp_proto,
                                 ps_string_from_cstr("test"),
                                 ps_value_object(test_fn),
                                 PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
            }
        }
        ps_object_define(vm->global,
                         ps_string_from_cstr("RegExp"),
                         ps_value_object(regexp_ctor),
                         PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
    }

    if (vm->math_obj) {
        ps_object_define(vm->math_obj,
                         ps_string_from_cstr("E"),
                         ps_value_number(2.718281828459045),
                         PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
        ps_object_define(vm->math_obj,
                         ps_string_from_cstr("LN10"),
                         ps_value_number(2.302585092994046),
                         PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
        ps_object_define(vm->math_obj,
                         ps_string_from_cstr("LN2"),
                         ps_value_number(0.6931471805599453),
                         PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
        ps_object_define(vm->math_obj,
                         ps_string_from_cstr("LOG2E"),
                         ps_value_number(1.4426950408889634),
                         PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
        ps_object_define(vm->math_obj,
                         ps_string_from_cstr("LOG10E"),
                         ps_value_number(0.4342944819032518),
                         PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
        ps_object_define(vm->math_obj,
                         ps_string_from_cstr("PI"),
                         ps_value_number(3.141592653589793),
                         PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
        ps_object_define(vm->math_obj,
                         ps_string_from_cstr("SQRT1_2"),
                         ps_value_number(0.7071067811865476),
                         PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
        ps_object_define(vm->math_obj,
                         ps_string_from_cstr("SQRT2"),
                         ps_value_number(1.4142135623730951),
                         PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
        PSObject *abs_fn = ps_function_new_native(ps_native_math_abs);
        if (abs_fn) {
            ps_function_setup(abs_fn, vm->function_proto, vm->object_proto, NULL);
            ps_object_define(vm->math_obj, ps_string_from_cstr("abs"), ps_value_object(abs_fn),
                             PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
        }
        PSObject *acos_fn = ps_function_new_native(ps_native_math_acos);
        if (acos_fn) {
            ps_function_setup(acos_fn, vm->function_proto, vm->object_proto, NULL);
            ps_object_define(vm->math_obj, ps_string_from_cstr("acos"), ps_value_object(acos_fn),
                             PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
        }
        PSObject *asin_fn = ps_function_new_native(ps_native_math_asin);
        if (asin_fn) {
            ps_function_setup(asin_fn, vm->function_proto, vm->object_proto, NULL);
            ps_object_define(vm->math_obj, ps_string_from_cstr("asin"), ps_value_object(asin_fn),
                             PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
        }
        PSObject *atan_fn = ps_function_new_native(ps_native_math_atan);
        if (atan_fn) {
            ps_function_setup(atan_fn, vm->function_proto, vm->object_proto, NULL);
            ps_object_define(vm->math_obj, ps_string_from_cstr("atan"), ps_value_object(atan_fn),
                             PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
        }
        PSObject *atan2_fn = ps_function_new_native(ps_native_math_atan2);
        if (atan2_fn) {
            ps_function_setup(atan2_fn, vm->function_proto, vm->object_proto, NULL);
            ps_object_define(vm->math_obj, ps_string_from_cstr("atan2"), ps_value_object(atan2_fn),
                             PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
        }
        PSObject *floor_fn = ps_function_new_native(ps_native_math_floor);
        if (floor_fn) {
            ps_function_setup(floor_fn, vm->function_proto, vm->object_proto, NULL);
            ps_object_define(vm->math_obj, ps_string_from_cstr("floor"), ps_value_object(floor_fn),
                             PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
        }
        PSObject *cos_fn = ps_function_new_native(ps_native_math_cos);
        if (cos_fn) {
            ps_function_setup(cos_fn, vm->function_proto, vm->object_proto, NULL);
            ps_object_define(vm->math_obj, ps_string_from_cstr("cos"), ps_value_object(cos_fn),
                             PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
        }
        PSObject *exp_fn = ps_function_new_native(ps_native_math_exp);
        if (exp_fn) {
            ps_function_setup(exp_fn, vm->function_proto, vm->object_proto, NULL);
            ps_object_define(vm->math_obj, ps_string_from_cstr("exp"), ps_value_object(exp_fn),
                             PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
        }
        PSObject *ceil_fn = ps_function_new_native(ps_native_math_ceil);
        if (ceil_fn) {
            ps_function_setup(ceil_fn, vm->function_proto, vm->object_proto, NULL);
            ps_object_define(vm->math_obj, ps_string_from_cstr("ceil"), ps_value_object(ceil_fn),
                             PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
        }
        PSObject *max_fn = ps_function_new_native(ps_native_math_max);
        if (max_fn) {
            ps_function_setup(max_fn, vm->function_proto, vm->object_proto, NULL);
            ps_object_define(vm->math_obj, ps_string_from_cstr("max"), ps_value_object(max_fn),
                             PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
        }
        PSObject *min_fn = ps_function_new_native(ps_native_math_min);
        if (min_fn) {
            ps_function_setup(min_fn, vm->function_proto, vm->object_proto, NULL);
            ps_object_define(vm->math_obj, ps_string_from_cstr("min"), ps_value_object(min_fn),
                             PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
        }
        PSObject *log_fn = ps_function_new_native(ps_native_math_log);
        if (log_fn) {
            ps_function_setup(log_fn, vm->function_proto, vm->object_proto, NULL);
            ps_object_define(vm->math_obj, ps_string_from_cstr("log"), ps_value_object(log_fn),
                             PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
        }
        PSObject *pow_fn = ps_function_new_native(ps_native_math_pow);
        if (pow_fn) {
            ps_function_setup(pow_fn, vm->function_proto, vm->object_proto, NULL);
            ps_object_define(vm->math_obj, ps_string_from_cstr("pow"), ps_value_object(pow_fn),
                             PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
        }
        PSObject *round_fn = ps_function_new_native(ps_native_math_round);
        if (round_fn) {
            ps_function_setup(round_fn, vm->function_proto, vm->object_proto, NULL);
            ps_object_define(vm->math_obj, ps_string_from_cstr("round"), ps_value_object(round_fn),
                             PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
        }
        PSObject *sin_fn = ps_function_new_native(ps_native_math_sin);
        if (sin_fn) {
            ps_function_setup(sin_fn, vm->function_proto, vm->object_proto, NULL);
            ps_object_define(vm->math_obj, ps_string_from_cstr("sin"), ps_value_object(sin_fn),
                             PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
        }
        PSObject *sqrt_fn = ps_function_new_native(ps_native_math_sqrt);
        if (sqrt_fn) {
            ps_function_setup(sqrt_fn, vm->function_proto, vm->object_proto, NULL);
            ps_object_define(vm->math_obj, ps_string_from_cstr("sqrt"), ps_value_object(sqrt_fn),
                             PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
        }
        PSObject *tan_fn = ps_function_new_native(ps_native_math_tan);
        if (tan_fn) {
            ps_function_setup(tan_fn, vm->function_proto, vm->object_proto, NULL);
            ps_object_define(vm->math_obj, ps_string_from_cstr("tan"), ps_value_object(tan_fn),
                             PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
        }
        PSObject *random_fn = ps_function_new_native(ps_native_math_random);
        if (random_fn) {
            ps_function_setup(random_fn, vm->function_proto, vm->object_proto, NULL);
            ps_object_define(vm->math_obj, ps_string_from_cstr("random"), ps_value_object(random_fn),
                             PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
        }
        ps_object_define(vm->global,
                         ps_string_from_cstr("Math"),
                         ps_value_object(vm->math_obj),
                         PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
        vm->math_intrinsics_valid = 1;
    }

    PSObject *json_obj = ps_object_new(vm->object_proto);
    if (json_obj) {
        PSObject *parse_fn = ps_function_new_native(ps_native_json_parse);
        if (parse_fn) {
            ps_function_setup(parse_fn, vm->function_proto, vm->object_proto, NULL);
            ps_define_function_props(parse_fn, "parse", 1);
            ps_object_define(json_obj, ps_string_from_cstr("parse"),
                             ps_value_object(parse_fn), PS_ATTR_DONTENUM);
        }
        PSObject *stringify_fn = ps_function_new_native(ps_native_json_stringify);
        if (stringify_fn) {
            ps_function_setup(stringify_fn, vm->function_proto, vm->object_proto, NULL);
            ps_define_function_props(stringify_fn, "stringify", 1);
            ps_object_define(json_obj, ps_string_from_cstr("stringify"),
                             ps_value_object(stringify_fn), PS_ATTR_DONTENUM);
        }
        ps_object_define(vm->global,
                         ps_string_from_cstr("JSON"),
                         ps_value_object(json_obj),
                         PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
    }

    PSObject *gc_obj = ps_object_new(vm->object_proto);
    if (gc_obj) {
        PSObject *collect_fn = ps_function_new_native(ps_native_gc_collect);
        if (collect_fn) {
            ps_function_setup(collect_fn, vm->function_proto, vm->object_proto, NULL);
            ps_define_function_props(collect_fn, "collect", 0);
            ps_object_define(gc_obj, ps_string_from_cstr("collect"),
                             ps_value_object(collect_fn), PS_ATTR_DONTENUM);
        }
        PSObject *stats_fn = ps_function_new_native(ps_native_gc_stats);
        if (stats_fn) {
            ps_function_setup(stats_fn, vm->function_proto, vm->object_proto, NULL);
            ps_define_function_props(stats_fn, "stats", 0);
            ps_object_define(gc_obj, ps_string_from_cstr("stats"),
                             ps_value_object(stats_fn), PS_ATTR_DONTENUM);
        }
        ps_object_define(vm->global,
                         ps_string_from_cstr("Gc"),
                         ps_value_object(gc_obj),
                         PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
    }
}

void ps_vm_init_io(PSVM *vm) {
    /* Delegate to io.c */
    ps_io_init(vm);
}

#if PS_ENABLE_MODULE_FS
void ps_vm_init_fs(PSVM *vm) {
    /* Delegate to fs.c */
    ps_fs_init(vm);
}
#endif

#if PS_ENABLE_MODULE_IMG
void ps_vm_init_img(PSVM *vm) {
    /* Delegate to img.c */
    ps_img_init(vm);
}
#endif

void ps_vm_init_buffer(PSVM *vm) {
    /* Delegate to buffer.c */
    ps_buffer_init(vm);
}

void ps_vm_init_event(PSVM *vm) {
    /* Delegate to event.c */
    ps_event_init(vm);
}

void ps_vm_init_display(PSVM *vm) {
    /* Delegate to display.c */
    ps_display_init(vm);
}
