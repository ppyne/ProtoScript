#include "ps_event.h"
#include "ps_display.h"
#include "ps_function.h"
#include "ps_object.h"
#include "ps_string.h"
#include "ps_value.h"

#include <stddef.h>

static PSValue ps_native_event_next(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    if (!vm || !vm->event_queue) return ps_value_null();
    ps_display_poll_events(vm);
    if (vm->event_count == 0) return ps_value_null();
    PSValue value = vm->event_queue[vm->event_head];
    vm->event_head = (vm->event_head + 1) % vm->event_capacity;
    vm->event_count--;
    return value;
}

static PSValue ps_native_event_clear(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    if (!vm) return ps_value_undefined();
    vm->event_head = 0;
    vm->event_tail = 0;
    vm->event_count = 0;
    return ps_value_undefined();
}

void ps_event_init(PSVM *vm) {
    if (!vm || !vm->global) return;
    PSObject *event = ps_object_new(NULL);
    if (!event) return;

    PSObject *next_fn = ps_function_new_native(ps_native_event_next);
    PSObject *clear_fn = ps_function_new_native(ps_native_event_clear);
    if (next_fn) ps_function_setup(next_fn, vm->function_proto, vm->object_proto, NULL);
    if (clear_fn) ps_function_setup(clear_fn, vm->function_proto, vm->object_proto, NULL);

    if (next_fn) {
        ps_object_define(event, ps_string_from_cstr("next"), ps_value_object(next_fn), PS_ATTR_NONE);
    }
    if (clear_fn) {
        ps_object_define(event, ps_string_from_cstr("clear"), ps_value_object(clear_fn), PS_ATTR_NONE);
    }

    ps_object_define(vm->global,
                     ps_string_from_cstr("Event"),
                     ps_value_object(event),
                     PS_ATTR_NONE);
}

int ps_event_push_value(PSVM *vm, PSValue value) {
    if (!vm || !vm->event_queue || vm->event_capacity == 0) return 0;
    if (vm->event_count == vm->event_capacity) {
        vm->event_head = (vm->event_head + 1) % vm->event_capacity;
        vm->event_count--;
    }
    vm->event_queue[vm->event_tail] = value;
    vm->event_tail = (vm->event_tail + 1) % vm->event_capacity;
    vm->event_count++;
    return 1;
}

int ps_event_push(PSVM *vm, const char *type) {
    if (!vm) return 0;
    PSObject *obj = ps_object_new(vm->object_proto);
    if (!obj) return 0;
    ps_object_define(obj,
                     ps_string_from_cstr("type"),
                     ps_value_string(ps_string_from_cstr(type ? type : "")),
                     PS_ATTR_NONE);
    return ps_event_push_value(vm, ps_value_object(obj));
}
