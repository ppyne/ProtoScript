#include "ps_gc.h"
#include "ps_config.h"

#include <stdlib.h>
#include "ps_vm.h"
#include "ps_object.h"
#include "ps_string.h"
#include "ps_env.h"
#include "ps_function.h"
#include "ps_ast.h"
#include "ps_regexp.h"
#include "ps_buffer.h"
#include "ps_display.h"
#include "ps_config.h"
#if PS_ENABLE_MODULE_IMG
#include "ps_img.h"
#endif

#include <stdlib.h>
#include <string.h>

static const uint32_t PS_GC_MAGIC = 0x50474331; /* "PGC1" */

static PSVM *g_active_vm = NULL;

static PSGCHeader *ps_gc_header(void *ptr) {
    return ptr ? ((PSGCHeader *)ptr - 1) : NULL;
}

static int ps_gc_header_valid(const PSGCHeader *hdr) {
    return hdr && hdr->magic == PS_GC_MAGIC;
}

void ps_gc_set_active_vm(PSVM *vm) {
    g_active_vm = vm;
}

PSVM *ps_gc_active_vm(void) {
    return g_active_vm;
}

void ps_gc_init(PSVM *vm) {
    if (!vm) return;
    memset(&vm->gc, 0, sizeof(vm->gc));
    vm->gc.min_threshold = 256 * 1024;
    vm->gc.growth_factor = 2.0;
    vm->gc.threshold = vm->gc.min_threshold;
}

void ps_gc_destroy(PSVM *vm) {
    if (!vm) return;
    PSGCHeader *hdr = vm->gc.head;
    while (hdr) {
        PSGCHeader *next = hdr->next;
        ps_gc_free(hdr + 1);
        hdr = next;
    }
    free(vm->gc.roots);
    vm->gc.roots = NULL;
    vm->gc.root_count = 0;
    vm->gc.root_cap = 0;
}

void *ps_gc_alloc_vm(PSVM *vm, PSGCType type, size_t size) {
    if (!vm) return NULL;
    PSGCHeader *hdr = (PSGCHeader *)calloc(1, sizeof(PSGCHeader) + size);
    if (!hdr) return NULL;
    hdr->magic = PS_GC_MAGIC;
    hdr->marked = 0;
    hdr->type = (uint8_t)type;
    hdr->size = size;
    hdr->next = vm->gc.head;
    vm->gc.head = hdr;
    vm->gc.heap_bytes += size;
    vm->gc.bytes_since_gc += size;
#if PS_ENABLE_PERF
    vm->perf.alloc_count++;
    vm->perf.alloc_bytes += (uint64_t)size;
#endif
    if (vm->gc.bytes_since_gc >= vm->gc.threshold) {
        vm->gc.should_collect = 1;
    }
    return hdr + 1;
}

void *ps_gc_alloc(PSGCType type, size_t size) {
    PSVM *vm = ps_gc_active_vm();
    if (!vm) return calloc(1, size);
    return ps_gc_alloc_vm(vm, type, size);
}

int ps_gc_is_managed(const void *ptr) {
    PSVM *vm = ps_gc_active_vm();
    if (!vm || !ptr) return 0;
    for (PSGCHeader *hdr = vm->gc.head; hdr; hdr = hdr->next) {
        if ((const void *)(hdr + 1) == ptr) return 1;
    }
    return 0;
}

static void ps_gc_mark_value(PSVM *vm, PSValue value);
static void ps_gc_mark_object(PSVM *vm, PSObject *obj);
static void ps_gc_mark_string(PSVM *vm, PSString *str);
static void ps_gc_mark_env(PSVM *vm, PSEnv *env);
static void ps_gc_mark_function(PSVM *vm, PSFunction *fn);

static void ps_gc_mark_ast_node(PSVM *vm, PSAstNode *node) {
    if (!node) return;
    switch (node->kind) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (size_t i = 0; i < node->as.list.count; i++) {
                ps_gc_mark_ast_node(vm, node->as.list.items[i]);
            }
            break;
        case AST_VAR_DECL:
            ps_gc_mark_ast_node(vm, node->as.var_decl.id);
            ps_gc_mark_ast_node(vm, node->as.var_decl.init);
            break;
        case AST_EXPR_STMT:
            ps_gc_mark_ast_node(vm, node->as.expr_stmt.expr);
            break;
        case AST_RETURN:
            ps_gc_mark_ast_node(vm, node->as.ret.expr);
            break;
        case AST_IF:
            ps_gc_mark_ast_node(vm, node->as.if_stmt.cond);
            ps_gc_mark_ast_node(vm, node->as.if_stmt.then_branch);
            ps_gc_mark_ast_node(vm, node->as.if_stmt.else_branch);
            break;
        case AST_WHILE:
            ps_gc_mark_ast_node(vm, node->as.while_stmt.cond);
            ps_gc_mark_ast_node(vm, node->as.while_stmt.body);
            ps_gc_mark_ast_node(vm, node->as.while_stmt.label);
            break;
        case AST_DO_WHILE:
            ps_gc_mark_ast_node(vm, node->as.do_while.body);
            ps_gc_mark_ast_node(vm, node->as.do_while.cond);
            ps_gc_mark_ast_node(vm, node->as.do_while.label);
            break;
        case AST_FOR:
            ps_gc_mark_ast_node(vm, node->as.for_stmt.init);
            ps_gc_mark_ast_node(vm, node->as.for_stmt.test);
            ps_gc_mark_ast_node(vm, node->as.for_stmt.update);
            ps_gc_mark_ast_node(vm, node->as.for_stmt.body);
            ps_gc_mark_ast_node(vm, node->as.for_stmt.label);
            break;
        case AST_FOR_IN:
            ps_gc_mark_ast_node(vm, node->as.for_in.target);
            ps_gc_mark_ast_node(vm, node->as.for_in.object);
            ps_gc_mark_ast_node(vm, node->as.for_in.body);
            ps_gc_mark_ast_node(vm, node->as.for_in.label);
            break;
        case AST_SWITCH:
            ps_gc_mark_ast_node(vm, node->as.switch_stmt.expr);
            for (size_t i = 0; i < node->as.switch_stmt.case_count; i++) {
                ps_gc_mark_ast_node(vm, node->as.switch_stmt.cases[i]);
            }
            ps_gc_mark_ast_node(vm, node->as.switch_stmt.label);
            break;
        case AST_CASE:
            ps_gc_mark_ast_node(vm, node->as.case_stmt.test);
            for (size_t i = 0; i < node->as.case_stmt.count; i++) {
                ps_gc_mark_ast_node(vm, node->as.case_stmt.items[i]);
            }
            break;
        case AST_LABEL:
            ps_gc_mark_ast_node(vm, node->as.label_stmt.label);
            ps_gc_mark_ast_node(vm, node->as.label_stmt.stmt);
            break;
        case AST_BREAK:
        case AST_CONTINUE:
            ps_gc_mark_ast_node(vm, node->as.jump_stmt.label);
            break;
        case AST_WITH:
            ps_gc_mark_ast_node(vm, node->as.with_stmt.object);
            ps_gc_mark_ast_node(vm, node->as.with_stmt.body);
            break;
        case AST_THROW:
            ps_gc_mark_ast_node(vm, node->as.throw_stmt.expr);
            break;
        case AST_TRY:
            ps_gc_mark_ast_node(vm, node->as.try_stmt.try_block);
            ps_gc_mark_ast_node(vm, node->as.try_stmt.catch_param);
            ps_gc_mark_ast_node(vm, node->as.try_stmt.catch_block);
            ps_gc_mark_ast_node(vm, node->as.try_stmt.finally_block);
            break;
        case AST_FUNCTION_DECL:
            ps_gc_mark_ast_node(vm, node->as.func_decl.id);
            for (size_t i = 0; i < node->as.func_decl.param_count; i++) {
                ps_gc_mark_ast_node(vm, node->as.func_decl.params[i]);
                if (node->as.func_decl.param_defaults) {
                    ps_gc_mark_ast_node(vm, node->as.func_decl.param_defaults[i]);
                }
            }
            ps_gc_mark_ast_node(vm, node->as.func_decl.body);
            break;
        case AST_FUNCTION_EXPR:
            ps_gc_mark_ast_node(vm, node->as.func_expr.id);
            for (size_t i = 0; i < node->as.func_expr.param_count; i++) {
                ps_gc_mark_ast_node(vm, node->as.func_expr.params[i]);
                if (node->as.func_expr.param_defaults) {
                    ps_gc_mark_ast_node(vm, node->as.func_expr.param_defaults[i]);
                }
            }
            ps_gc_mark_ast_node(vm, node->as.func_expr.body);
            break;
        case AST_IDENTIFIER:
            if (node->as.identifier.str) {
                ps_gc_mark_string(vm, node->as.identifier.str);
            }
            break;
        case AST_LITERAL:
            ps_gc_mark_value(vm, node->as.literal.value);
            break;
        case AST_ASSIGN:
            ps_gc_mark_ast_node(vm, node->as.assign.target);
            ps_gc_mark_ast_node(vm, node->as.assign.value);
            break;
        case AST_BINARY:
            ps_gc_mark_ast_node(vm, node->as.binary.left);
            ps_gc_mark_ast_node(vm, node->as.binary.right);
            break;
        case AST_UNARY:
            ps_gc_mark_ast_node(vm, node->as.unary.expr);
            break;
        case AST_UPDATE:
            ps_gc_mark_ast_node(vm, node->as.update.expr);
            break;
        case AST_CONDITIONAL:
            ps_gc_mark_ast_node(vm, node->as.conditional.cond);
            ps_gc_mark_ast_node(vm, node->as.conditional.then_expr);
            ps_gc_mark_ast_node(vm, node->as.conditional.else_expr);
            break;
        case AST_CALL:
            ps_gc_mark_ast_node(vm, node->as.call.callee);
            for (size_t i = 0; i < node->as.call.argc; i++) {
                ps_gc_mark_ast_node(vm, node->as.call.args[i]);
            }
            break;
        case AST_MEMBER:
            ps_gc_mark_ast_node(vm, node->as.member.object);
            ps_gc_mark_ast_node(vm, node->as.member.property);
            break;
        case AST_NEW:
            ps_gc_mark_ast_node(vm, node->as.new_expr.callee);
            for (size_t i = 0; i < node->as.new_expr.argc; i++) {
                ps_gc_mark_ast_node(vm, node->as.new_expr.args[i]);
            }
            break;
        case AST_ARRAY_LITERAL:
            for (size_t i = 0; i < node->as.array_literal.count; i++) {
                if (node->as.array_literal.items[i]) {
                    ps_gc_mark_ast_node(vm, node->as.array_literal.items[i]);
                }
            }
            break;
        case AST_OBJECT_LITERAL:
            for (size_t i = 0; i < node->as.object_literal.count; i++) {
                PSAstProperty *prop = &node->as.object_literal.props[i];
                ps_gc_mark_string(vm, prop->key);
                ps_gc_mark_ast_node(vm, prop->value);
            }
            break;
        default:
            break;
    }
}

static void ps_gc_mark_value(PSVM *vm, PSValue value) {
    if (value.type == PS_T_STRING) {
        ps_gc_mark_string(vm, value.as.string);
    } else if (value.type == PS_T_OBJECT) {
        ps_gc_mark_object(vm, value.as.object);
    }
}

static void ps_gc_mark_string(PSVM *vm, PSString *str) {
    (void)vm;
    if (!str) return;
    if (!ps_gc_is_managed(str)) return;
    PSGCHeader *hdr = ps_gc_header(str);
    if (!ps_gc_header_valid(hdr) || hdr->marked) return;
    hdr->marked = 1;
}

static void ps_gc_mark_function(PSVM *vm, PSFunction *fn) {
    if (!fn) return;
    if (!ps_gc_is_managed(fn)) return;
    PSGCHeader *hdr = ps_gc_header(fn);
    if (!ps_gc_header_valid(hdr) || hdr->marked) return;
    hdr->marked = 1;
    ps_gc_mark_env(vm, fn->env);
    ps_gc_mark_ast_node(vm, fn->body);
    for (size_t i = 0; i < fn->param_count; i++) {
        ps_gc_mark_ast_node(vm, fn->params[i]);
        if (fn->param_defaults) {
            ps_gc_mark_ast_node(vm, fn->param_defaults[i]);
        }
    }
    if (fn->param_names) {
        for (size_t i = 0; i < fn->param_count; i++) {
            ps_gc_mark_string(vm, fn->param_names[i]);
        }
    }
    ps_gc_mark_string(vm, fn->name);
}

static void ps_gc_mark_env(PSVM *vm, PSEnv *env) {
    if (!env) return;
    if (!ps_gc_is_managed(env)) return;
    PSGCHeader *hdr = ps_gc_header(env);
    if (!ps_gc_header_valid(hdr) || hdr->marked) return;
    hdr->marked = 1;
    ps_gc_mark_env(vm, env->parent);
    ps_gc_mark_object(vm, env->record);
    ps_gc_mark_object(vm, env->arguments_obj);
    ps_gc_mark_object(vm, env->callee_obj);
    if (env->param_names) {
        for (size_t i = 0; i < env->param_count; i++) {
            ps_gc_mark_string(vm, env->param_names[i]);
        }
    }
}

static void ps_gc_mark_object(PSVM *vm, PSObject *obj) {
    if (!obj) return;
    if (!ps_gc_is_managed(obj)) return;
    PSGCHeader *hdr = ps_gc_header(obj);
    if (!ps_gc_header_valid(hdr) || hdr->marked) return;
    hdr->marked = 1;
    ps_gc_mark_object(vm, obj->prototype);
    for (PSProperty *p = obj->props; p; p = p->next) {
        ps_gc_mark_string(vm, p->name);
        ps_gc_mark_value(vm, p->value);
    }
    if (!obj->internal) return;
    switch (obj->kind) {
        case PS_OBJ_KIND_FUNCTION:
            ps_gc_mark_function(vm, (PSFunction *)obj->internal);
            break;
        case PS_OBJ_KIND_BOOLEAN:
        case PS_OBJ_KIND_NUMBER:
        case PS_OBJ_KIND_STRING:
        case PS_OBJ_KIND_DATE: {
            PSValue *inner = (PSValue *)obj->internal;
            if (inner) ps_gc_mark_value(vm, *inner);
            break;
        }
        case PS_OBJ_KIND_REGEXP: {
            PSRegex *re = (PSRegex *)obj->internal;
            if (re) ps_gc_mark_string(vm, re->source);
            break;
        }
        default:
            break;
    }
}

static void ps_gc_mark_roots(PSVM *vm) {
    if (!vm) return;
    ps_gc_mark_object(vm, vm->global);
    ps_gc_mark_env(vm, vm->env);
    ps_gc_mark_object(vm, vm->object_proto);
    ps_gc_mark_object(vm, vm->function_proto);
    ps_gc_mark_object(vm, vm->boolean_proto);
    ps_gc_mark_object(vm, vm->number_proto);
    ps_gc_mark_object(vm, vm->string_proto);
    ps_gc_mark_object(vm, vm->array_proto);
    ps_gc_mark_object(vm, vm->date_proto);
    ps_gc_mark_object(vm, vm->regexp_proto);
    ps_gc_mark_object(vm, vm->math_obj);
    ps_gc_mark_object(vm, vm->error_proto);
    ps_gc_mark_object(vm, vm->type_error_proto);
    ps_gc_mark_object(vm, vm->range_error_proto);
    ps_gc_mark_object(vm, vm->reference_error_proto);
    ps_gc_mark_object(vm, vm->syntax_error_proto);
    ps_gc_mark_object(vm, vm->eval_error_proto);
    ps_gc_mark_object(vm, vm->current_callee);
    if (vm->has_pending_throw) {
        ps_gc_mark_value(vm, vm->pending_throw);
    }
    if (vm->root_ast) {
        ps_gc_mark_ast_node(vm, vm->root_ast);
    } else {
        ps_gc_mark_ast_node(vm, vm->current_ast);
    }
    if (vm->display && vm->display->framebuffer_obj) {
        ps_gc_mark_object(vm, vm->display->framebuffer_obj);
    }
    if (vm->event_queue && vm->event_count > 0) {
        for (size_t i = 0; i < vm->event_count; i++) {
            size_t idx = (vm->event_head + i) % vm->event_capacity;
            ps_gc_mark_value(vm, vm->event_queue[idx]);
        }
    }
    for (size_t i = 0; i < vm->gc.root_count; i++) {
        PSGCRoot *root = &vm->gc.roots[i];
        switch (root->type) {
            case PS_GC_ROOT_VALUE:
                ps_gc_mark_value(vm, *(PSValue *)root->ptr);
                break;
            case PS_GC_ROOT_OBJECT:
                ps_gc_mark_object(vm, (PSObject *)root->ptr);
                break;
            case PS_GC_ROOT_STRING:
                ps_gc_mark_string(vm, (PSString *)root->ptr);
                break;
            case PS_GC_ROOT_ENV:
                ps_gc_mark_env(vm, (PSEnv *)root->ptr);
                break;
            case PS_GC_ROOT_FUNCTION:
                ps_gc_mark_function(vm, (PSFunction *)root->ptr);
                break;
            default:
                break;
        }
    }
}

static void ps_gc_finalize_object(PSObject *obj) {
    if (!obj) return;
    PSProperty *p = obj->props;
    while (p) {
        PSProperty *next = p->next;
        free(p);
        p = next;
    }
    obj->props = NULL;
    if (!obj->internal) return;
    switch (obj->kind) {
        case PS_OBJ_KIND_FUNCTION:
            /* PSFunction is GC-managed, no direct free here. */
            break;
        case PS_OBJ_KIND_BOOLEAN:
        case PS_OBJ_KIND_NUMBER:
        case PS_OBJ_KIND_STRING:
        case PS_OBJ_KIND_DATE:
            free(obj->internal);
            break;
        case PS_OBJ_KIND_REGEXP:
            ps_regex_free((PSRegex *)obj->internal);
            break;
        case PS_OBJ_KIND_BUFFER: {
            PSBuffer *buf = (PSBuffer *)obj->internal;
            if (buf) {
                free(buf->data);
                free(buf);
            }
            break;
        }
#if PS_ENABLE_MODULE_IMG
        case PS_OBJ_KIND_IMAGE:
            ps_img_handle_release((PSImageHandle *)obj->internal);
            break;
#endif
        default: {
            free(obj->internal);
            break;
        }
    }
    obj->internal = NULL;
}

static void ps_gc_finalize_string(PSString *s) {
    if (!s) return;
    free(s->utf8);
    free(s->glyph_offsets);
    s->utf8 = NULL;
    s->glyph_offsets = NULL;
    s->byte_len = 0;
    s->glyph_count = 0;
}

static void ps_gc_finalize_env(PSEnv *env) {
    if (!env) return;
    if (env->param_names_owned) {
        free(env->param_names);
    }
    env->param_names = NULL;
    env->param_count = 0;
    env->param_names_owned = 0;
    free(env->fast_values);
    env->fast_values = NULL;
    env->fast_count = 0;
}

static void ps_gc_finalize_function(PSFunction *fn) {
    if (!fn) return;
    free(fn->param_names);
    fn->param_names = NULL;
    /* Function references are owned by AST or env; nothing to free here. */
}

void ps_gc_free(void *ptr) {
    PSVM *vm = ps_gc_active_vm();
    if (!vm || !ptr) return;
    PSGCHeader *prev = NULL;
    PSGCHeader *hdr = vm->gc.head;
    while (hdr) {
        if ((void *)(hdr + 1) == ptr) break;
        prev = hdr;
        hdr = hdr->next;
    }
    if (!hdr) return;
    if (prev) prev->next = hdr->next;
    else vm->gc.head = hdr->next;
    vm->gc.heap_bytes -= hdr->size;
    switch (hdr->type) {
        case PS_GC_OBJECT:
            ps_gc_finalize_object((PSObject *)(hdr + 1));
            break;
        case PS_GC_STRING:
            ps_gc_finalize_string((PSString *)(hdr + 1));
            break;
        case PS_GC_ENV:
            ps_gc_finalize_env((PSEnv *)(hdr + 1));
            break;
        case PS_GC_FUNCTION:
            ps_gc_finalize_function((PSFunction *)(hdr + 1));
            break;
        default:
            break;
    }
    free(hdr);
}

void ps_gc_collect(PSVM *vm) {
    if (!vm || vm->gc.in_collect) return;
    vm->gc.in_collect = 1;
    ps_gc_mark_roots(vm);

    size_t live_bytes = 0;
    int freed = 0;

    PSGCHeader *prev = NULL;
    PSGCHeader *hdr = vm->gc.head;
    while (hdr) {
        PSGCHeader *next = hdr->next;
        if (!hdr->marked) {
            if (prev) prev->next = next;
            else vm->gc.head = next;
            vm->gc.heap_bytes -= hdr->size;
            switch (hdr->type) {
                case PS_GC_OBJECT:
                    ps_gc_finalize_object((PSObject *)(hdr + 1));
                    break;
                case PS_GC_STRING:
                    ps_gc_finalize_string((PSString *)(hdr + 1));
                    break;
                case PS_GC_ENV:
                    ps_gc_finalize_env((PSEnv *)(hdr + 1));
                    break;
                case PS_GC_FUNCTION:
                    ps_gc_finalize_function((PSFunction *)(hdr + 1));
                    break;
                default:
                    break;
            }
            free(hdr);
            freed++;
        } else {
            hdr->marked = 0;
            live_bytes += hdr->size;
            prev = hdr;
        }
        hdr = next;
    }

    vm->gc.live_bytes_last = live_bytes;
    vm->gc.freed_last = freed;
    vm->gc.collections++;
    vm->gc.bytes_since_gc = 0;
    vm->gc.threshold = live_bytes ? (size_t)(live_bytes * vm->gc.growth_factor) : vm->gc.min_threshold;
    if (vm->gc.threshold < vm->gc.min_threshold) {
        vm->gc.threshold = vm->gc.min_threshold;
    }
    vm->gc.should_collect = 0;
    vm->gc.in_collect = 0;
}

void ps_gc_safe_point(PSVM *vm) {
    if (!vm) return;
    if (vm->gc.should_collect && !vm->gc.in_collect) {
        ps_gc_collect(vm);
    }
}

void ps_gc_root_push(PSVM *vm, PSGCRootType type, void *ptr) {
    if (!vm) return;
    if (vm->gc.root_count == vm->gc.root_cap) {
        size_t new_cap = vm->gc.root_cap ? vm->gc.root_cap * 2 : 16;
        PSGCRoot *next = (PSGCRoot *)realloc(vm->gc.roots, sizeof(PSGCRoot) * new_cap);
        if (!next) return;
        vm->gc.roots = next;
        vm->gc.root_cap = new_cap;
    }
    vm->gc.roots[vm->gc.root_count].type = type;
    vm->gc.roots[vm->gc.root_count].ptr = ptr;
    vm->gc.root_count++;
}

void ps_gc_root_pop(PSVM *vm, size_t count) {
    if (!vm) return;
    if (count > vm->gc.root_count) {
        vm->gc.root_count = 0;
        return;
    }
    vm->gc.root_count -= count;
}
