#include "ps_buffer.h"
#include "ps_eval.h"
#include "ps_function.h"
#include "ps_object.h"
#include "ps_string.h"
#include "ps_value.h"
#include "ps_vm.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static void buffer_throw(PSVM *vm, const char *name, const char *message) {
    if (!vm) return;
    vm->pending_throw = ps_vm_make_error(vm, name ? name : "Error", message ? message : "");
    vm->has_pending_throw = 1;
}

PSBuffer *ps_buffer_from_object(PSObject *obj) {
    if (!obj || obj->kind != PS_OBJ_KIND_BUFFER) return NULL;
    return (PSBuffer *)obj->internal;
}

PSObject *ps_buffer_new(PSVM *vm, size_t size) {
    PSObject *obj = ps_object_new(vm ? vm->object_proto : NULL);
    if (!obj) return NULL;
    PSBuffer *buf = (PSBuffer *)calloc(1, sizeof(PSBuffer));
    if (!buf) return NULL;
    if (size > 0) {
        buf->data = (uint8_t *)calloc(size, 1);
        if (!buf->data) {
            free(buf);
            return NULL;
        }
    }
    buf->size = size;
    obj->kind = PS_OBJ_KIND_BUFFER;
    obj->internal = buf;
    ps_object_define(obj,
                     ps_string_from_cstr("length"),
                     ps_value_number((double)size),
                     PS_ATTR_READONLY | PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
    return obj;
}

static int ps_buffer_parse_size(PSVM *vm, PSValue value, size_t *out_size) {
    double num = ps_to_number(vm, value);
    if (vm && vm->has_pending_throw) return 0;
    if (isnan(num) || isinf(num) || num < 0.0 || floor(num) != num) {
        buffer_throw(vm, "RangeError", "Invalid buffer size");
        return 0;
    }
    if (num > (double)SIZE_MAX) {
        buffer_throw(vm, "RangeError", "Invalid buffer size");
        return 0;
    }
    if (out_size) *out_size = (size_t)num;
    return 1;
}

static PSValue ps_native_buffer_alloc(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (argc < 1) {
        ps_vm_throw_type_error(vm, "Buffer.alloc expects (size)");
        return ps_value_undefined();
    }
    size_t size = 0;
    if (!ps_buffer_parse_size(vm, argv[0], &size)) return ps_value_undefined();
    PSObject *obj = ps_buffer_new(vm, size);
    if (!obj) return ps_value_undefined();
    return ps_value_object(obj);
}

static PSValue ps_native_buffer_size(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (argc < 1) {
        ps_vm_throw_type_error(vm, "Buffer.size expects (buffer)");
        return ps_value_undefined();
    }
    if (argv[0].type != PS_T_OBJECT || !argv[0].as.object) {
        ps_vm_throw_type_error(vm, "Buffer.size expects (buffer)");
        return ps_value_undefined();
    }
    PSBuffer *buf = ps_buffer_from_object(argv[0].as.object);
    if (!buf) {
        ps_vm_throw_type_error(vm, "Buffer.size expects (buffer)");
        return ps_value_undefined();
    }
    return ps_value_number((double)buf->size);
}

static PSValue ps_native_buffer_slice(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (argc < 3) {
        ps_vm_throw_type_error(vm, "Buffer.slice expects (buffer, offset, length)");
        return ps_value_undefined();
    }
    if (argv[0].type != PS_T_OBJECT || !argv[0].as.object) {
        ps_vm_throw_type_error(vm, "Buffer.slice expects (buffer, offset, length)");
        return ps_value_undefined();
    }
    PSBuffer *buf = ps_buffer_from_object(argv[0].as.object);
    if (!buf) {
        ps_vm_throw_type_error(vm, "Buffer.slice expects (buffer, offset, length)");
        return ps_value_undefined();
    }
    size_t offset = 0;
    size_t length = 0;
    if (!ps_buffer_parse_size(vm, argv[1], &offset)) return ps_value_undefined();
    if (!ps_buffer_parse_size(vm, argv[2], &length)) return ps_value_undefined();
    if (offset > buf->size || length > buf->size - offset) {
        buffer_throw(vm, "RangeError", "Invalid buffer slice");
        return ps_value_undefined();
    }
    PSObject *out = ps_buffer_new(vm, length);
    if (!out) return ps_value_undefined();
    PSBuffer *out_buf = ps_buffer_from_object(out);
    if (length > 0 && out_buf && buf->data) {
        memcpy(out_buf->data, buf->data + offset, length);
    }
    return ps_value_object(out);
}

void ps_buffer_init(PSVM *vm) {
    if (!vm || !vm->global) return;

    PSObject *buffer = ps_object_new(NULL);
    if (!buffer) return;

    PSObject *alloc_fn = ps_function_new_native(ps_native_buffer_alloc);
    PSObject *size_fn = ps_function_new_native(ps_native_buffer_size);
    PSObject *slice_fn = ps_function_new_native(ps_native_buffer_slice);

    if (alloc_fn) ps_function_setup(alloc_fn, vm->function_proto, vm->object_proto, NULL);
    if (size_fn) ps_function_setup(size_fn, vm->function_proto, vm->object_proto, NULL);
    if (slice_fn) ps_function_setup(slice_fn, vm->function_proto, vm->object_proto, NULL);

    if (alloc_fn) {
        ps_object_define(buffer, ps_string_from_cstr("alloc"), ps_value_object(alloc_fn), PS_ATTR_NONE);
    }
    if (size_fn) {
        ps_object_define(buffer, ps_string_from_cstr("size"), ps_value_object(size_fn), PS_ATTR_NONE);
    }
    if (slice_fn) {
        ps_object_define(buffer, ps_string_from_cstr("slice"), ps_value_object(slice_fn), PS_ATTR_NONE);
    }

    ps_object_define(vm->global,
                     ps_string_from_cstr("Buffer"),
                     ps_value_object(buffer),
                     PS_ATTR_NONE);
}
