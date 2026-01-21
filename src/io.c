#include "ps_vm.h"
#include "ps_object.h"
#include "ps_value.h"
#include "ps_string.h"
#include "ps_function.h"
#include "ps_eval.h"

#include <stdio.h>
#include <stdlib.h>

/* --------------------------------------------------------- */
/* Internal builtin: Io.print                                */
/* --------------------------------------------------------- */

/* Native implementation of Io.print */
static PSValue ps_native_print(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    if (argc > 0) {
        PSString *s = ps_to_string(vm, argv[0]);
        if (s) {
            fwrite(s->utf8, 1, s->byte_len, stdout);
        }
    }
    fputc('\n', stdout);
    return ps_value_undefined();
}

/* --------------------------------------------------------- */
/* Io object initialization                                  */
/* --------------------------------------------------------- */

void ps_io_init(PSVM *vm) {
    if (!vm || !vm->global) return;

    /* Create Io object */
    PSObject *io = ps_object_new(NULL);
    if (!io) return;

    /* Create a function object for print */
    PSObject *print_fn = ps_function_new_native(ps_native_print);
    if (!print_fn) return;
    ps_function_setup(print_fn, vm->function_proto, vm->object_proto, NULL);

    /* Attach print to Io */
    ps_object_define(
        io,
        ps_string_from_cstr("print"),
        ps_value_object(print_fn),
        PS_ATTR_NONE
    );

    /* Attach Io to Global Object */
    ps_object_define(
        vm->global,
        ps_string_from_cstr("Io"),
        ps_value_object(io),
        PS_ATTR_NONE
    );
}
