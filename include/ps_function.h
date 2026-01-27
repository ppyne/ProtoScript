#ifndef PS_FUNCTION_H
#define PS_FUNCTION_H

#include <stddef.h>
#include <stdint.h>

#include "ps_value.h"
#include "ps_object.h"
#include "ps_config.h"

struct PSAstNode;
struct PSEnv;
struct PSVM;
struct PSFastNumOp;
struct PSStmtBC;

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
    size_t          *fast_local_index;
    size_t           fast_local_index_count;
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
    struct PSStmtBC *stmt_bc;
    uint8_t          stmt_bc_state;
#if !PS_DISABLE_SPECIALIZATION
    PSString        **slot_names;
    size_t           slot_count;
    PSString        **spec_slot_names;
    size_t           spec_slot_count;
    uint32_t         spec_hot_count;
    struct PSStmtBC *spec_bc;
    uint8_t          spec_bc_state;
    uint8_t          spec_guard_count;
    uint8_t          spec_guard_slots[PS_SPECIALIZATION_GUARD_MAX];
#endif
#if !PS_DISABLE_UNBOXED_SPEC
    uint32_t         unboxed_hot_count;
    struct PSStmtBC *unboxed_bc;
    uint8_t          unboxed_bc_state;
    uint8_t          unboxed_guard_count;
    uint8_t          unboxed_guard_slots[PS_SPECIALIZATION_GUARD_MAX];
    uint8_t          unboxed_used_count;
    uint8_t          unboxed_used_slots[PS_SPECIALIZATION_GUARD_MAX];
    uint32_t         unboxed_write_bits[(PS_SPECIALIZATION_SLOT_MAX + 31) / 32];
#endif
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
