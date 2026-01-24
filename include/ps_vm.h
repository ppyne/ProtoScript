#ifndef PS_VM_H
#define PS_VM_H

#include "ps_config.h"
#include "ps_object.h"
#include "ps_value.h"
#include "ps_env.h"
#include "ps_gc.h"

#include <stdint.h>

struct PSAstNode;
struct PSDisplay;

/*
 * ProtoScript Virtual Machine
 *
 * This structure represents a single execution environment.
 * It owns the global object and all host-provided bindings.
 */
typedef struct PSPerfStats {
    uint64_t alloc_count;
    uint64_t alloc_bytes;
    uint64_t object_new;
    uint64_t string_new;
    uint64_t function_new;
    uint64_t env_new;
    uint64_t call_count;
    uint64_t native_call_count;
} PSPerfStats;

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

    PSValue *event_queue;
    size_t event_capacity;
    size_t event_head;
    size_t event_tail;
    size_t event_count;

    struct PSDisplay *display;

    int has_pending_throw;
    PSValue pending_throw;
    PSObject *current_callee;
    int is_constructing;
    struct PSAstNode *root_ast;
    struct PSAstNode *current_ast;
    struct PSAstNode *current_node;
    PSString **index_cache;
    size_t index_cache_size;
    PSPerfStats perf;
    PSGC gc;
} PSVM;

/* VM lifecycle */
PSVM    *ps_vm_new(void);
void     ps_vm_free(PSVM *vm);

/* Accessors */
PSObject *ps_vm_global(PSVM *vm);

/* Initialization helpers */
void ps_vm_init_builtins(PSVM *vm);
void ps_vm_init_buffer(PSVM *vm);
void ps_vm_init_event(PSVM *vm);
void ps_vm_init_display(PSVM *vm);
void ps_vm_init_io(PSVM *vm);
#if PS_ENABLE_MODULE_FS
void ps_vm_init_fs(PSVM *vm);
#endif
#if PS_ENABLE_MODULE_IMG
void ps_vm_init_img(PSVM *vm);
#endif

/* Primitive wrappers */
PSObject *ps_vm_wrap_primitive(PSVM *vm, const PSValue *v);

/* Error helpers */
PSValue ps_vm_make_error(PSVM *vm, const char *name, const char *message);
void ps_vm_throw_type_error(PSVM *vm, const char *message);

#endif /* PS_VM_H */
