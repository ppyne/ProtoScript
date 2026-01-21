#ifndef PS_EVAL_H
#define PS_EVAL_H

#include "ps_vm.h"

typedef enum {
    PS_HINT_NONE = 0,
    PS_HINT_NUMBER,
    PS_HINT_STRING
} PSToPrimitiveHint;

PSValue ps_eval(PSVM *vm, struct PSAstNode *program);
PSValue ps_eval_call_function(PSVM *vm,
                              struct PSEnv *env,
                              PSObject *fn_obj,
                              PSValue this_val,
                              int argc,
                              PSValue *argv,
                              int *did_throw,
                              PSValue *throw_value);
PSValue ps_to_primitive(PSVM *vm, PSValue value, PSToPrimitiveHint hint);
PSString *ps_to_string(PSVM *vm, PSValue value);
double ps_to_number(PSVM *vm, PSValue value);
int ps_to_boolean(PSVM *vm, PSValue value);

#endif /* PS_EVAL_H */
