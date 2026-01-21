#include "ps_function.h"
#include "ps_env.h"
#include "ps_ast.h"
#include "ps_string.h"

#include <stdlib.h>

PSObject *ps_function_new_native(PSNativeFunc fn) {
    PSObject *obj = ps_object_new(NULL);
    if (!obj) return NULL;

    PSFunction *func = (PSFunction *)calloc(1, sizeof(PSFunction));
    if (!func) {
        ps_object_free(obj);
        return NULL;
    }
    func->is_native = 1;
    func->native = fn;

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

    PSFunction *func = (PSFunction *)calloc(1, sizeof(PSFunction));
    if (!func) {
        ps_object_free(obj);
        return NULL;
    }

    func->is_native = 0;
    func->params = params;
    func->param_defaults = param_defaults;
    func->param_count = param_count;
    func->body = body;
    func->env = env;

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
