#ifndef PS_FUNCTION_H
#define PS_FUNCTION_H

#include <stddef.h>
#include <stdint.h>

#include "ps_value.h"
#include "ps_object.h"

struct PSAstNode;
struct PSEnv;
struct PSVM;
struct PSFastNumOp;

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
    struct PSAstNode *fast_math_expr;
    struct PSAstNode **fast_num_inits;
    PSString         **fast_num_names;
    size_t           fast_num_count;
    struct PSAstNode *fast_num_return;
    struct PSAstNode *fast_num_if_cond;
    struct PSAstNode *fast_num_if_return;
    struct PSFastNumOp *fast_num_ops;
    size_t           fast_num_ops_count;
    double           fast_clamp_min;
    double           fast_clamp_max;
    uint8_t          fast_clamp_use_floor;
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
