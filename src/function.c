#include "ps_function.h"
#include "ps_env.h"
#include "ps_ast.h"
#include "ps_string.h"
#include "ps_gc.h"
#include "ps_config.h"
#include "ps_vm.h"

#include <stdlib.h>

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
    func->fast_flags = 0;
    func->fast_checked = 0;

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
