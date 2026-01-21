#ifndef PS_VM_H
#define PS_VM_H

#include "ps_object.h"
#include "ps_value.h"
#include "ps_env.h"

/*
 * ProtoScript Virtual Machine
 *
 * This structure represents a single execution environment.
 * It owns the global object and all host-provided bindings.
 */
typedef struct PSVM {
    PSObject *global;   /* Global Object */
    PSEnv    *env;      /* Current Environment */

    /* Builtins */
    PSObject *object_proto;
    PSObject *function_proto;
    PSObject *boolean_proto;
    PSObject *number_proto;
    PSObject *string_proto;
    PSObject *array_proto;
    PSObject *date_proto;
    PSObject *regexp_proto;
    PSObject *math_obj;
    PSObject *error_proto;
    PSObject *type_error_proto;
    PSObject *range_error_proto;
    PSObject *reference_error_proto;
    PSObject *syntax_error_proto;
    PSObject *eval_error_proto;

    int has_pending_throw;
    PSValue pending_throw;
    PSObject *current_callee;
    int is_constructing;
} PSVM;

/* VM lifecycle */
PSVM    *ps_vm_new(void);
void     ps_vm_free(PSVM *vm);

/* Accessors */
PSObject *ps_vm_global(PSVM *vm);

/* Initialization helpers */
void ps_vm_init_builtins(PSVM *vm);
void ps_vm_init_io(PSVM *vm);

/* Primitive wrappers */
PSObject *ps_vm_wrap_primitive(PSVM *vm, const PSValue *v);

/* Error helpers */
PSValue ps_vm_make_error(PSVM *vm, const char *name, const char *message);
void ps_vm_throw_type_error(PSVM *vm, const char *message);

#endif /* PS_VM_H */
