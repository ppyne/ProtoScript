#ifndef PS_FUNCTION_H
#define PS_FUNCTION_H

#include <stddef.h>
#include <stdint.h>

#include "ps_value.h"
#include "ps_object.h"

struct PSAstNode;
struct PSEnv;
struct PSVM;

typedef PSValue (*PSNativeFunc)(struct PSVM *vm, PSValue this_val, int argc, PSValue *argv);

typedef struct PSFunction {
    int              is_native;
    PSNativeFunc     native;
    struct PSAstNode *body;
    struct PSAstNode **params;
    struct PSAstNode **param_defaults;
    PSString         **param_names;
    PSString         *name;
    size_t           param_count;
    struct PSEnv    *env;
    PSString        **fast_names;
    size_t           fast_count;
    size_t          *fast_param_index;
    size_t           fast_local_count;
    size_t           fast_this_index;
    struct PSEnv    *fast_env;
    uint8_t          fast_env_in_use;
    uint8_t          fast_flags;
    uint8_t          fast_checked;
} PSFunction;

PSObject   *ps_function_new_native(PSNativeFunc fn);
PSObject   *ps_function_new_script(struct PSAstNode **params,
                                   struct PSAstNode **param_defaults,
                                   size_t param_count,
                                   struct PSAstNode *body,
                                   struct PSEnv *env);
PSFunction *ps_function_from_object(PSObject *obj);
void        ps_function_setup(PSObject *fn_obj,
                              PSObject *function_proto,
                              PSObject *object_proto,
                              PSObject *prototype_override);

#endif /* PS_FUNCTION_H */
