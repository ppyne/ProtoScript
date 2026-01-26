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

PSBuffer32 *ps_buffer32_from_object(PSObject *obj) {
    if (!obj || obj->kind != PS_OBJ_KIND_BUFFER32) return NULL;
    return (PSBuffer32 *)obj->internal;
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

static PSObject *ps_buffer32_new_internal(PSVM *vm,
                                          PSObject *buffer_obj,
                                          size_t offset,
                                          size_t length) {
    if (!buffer_obj) return NULL;
    PSBuffer *buf = ps_buffer_from_object(buffer_obj);
    if (!buf) return NULL;
    if (offset > buf->size) return NULL;
    if (length > (buf->size - offset) / 4u) return NULL;

    PSObject *obj = ps_object_new(vm ? vm->object_proto : NULL);
    if (!obj) return NULL;
    PSBuffer32 *view = (PSBuffer32 *)calloc(1, sizeof(PSBuffer32));
    if (!view) return NULL;

    view->source = buffer_obj;
    view->offset = offset;
    view->length = length;
    obj->kind = PS_OBJ_KIND_BUFFER32;
    obj->internal = view;

    ps_object_define(obj,
                     ps_string_from_cstr("length"),
                     ps_value_number((double)length),
                     PS_ATTR_READONLY | PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
    ps_object_define(obj,
                     ps_string_from_cstr("byteLength"),
                     ps_value_number((double)(length * 4u)),
                     PS_ATTR_READONLY | PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
    ps_object_define(obj,
                     ps_string_from_cstr("buffer"),
                     ps_value_object(buffer_obj),
                     PS_ATTR_READONLY | PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
    return obj;
}

PSObject *ps_buffer32_new(PSVM *vm, size_t length) {
    if (length > SIZE_MAX / 4u) return NULL;
    PSObject *buf_obj = ps_buffer_new(vm, length * 4u);
    if (!buf_obj) return NULL;
    return ps_buffer32_new_internal(vm, buf_obj, 0, length);
}

PSObject *ps_buffer32_view(PSVM *vm, PSObject *buffer_obj, size_t offset, size_t length) {
    (void)vm;
    return ps_buffer32_new_internal(vm, buffer_obj, offset, length);
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

static int ps_buffer32_parse_length(PSVM *vm, PSValue value, size_t *out_size) {
    if (!ps_buffer_parse_size(vm, value, out_size)) return 0;
    if (*out_size > SIZE_MAX / 4u) {
        buffer_throw(vm, "RangeError", "Invalid buffer32 length");
        return 0;
    }
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

static PSValue ps_native_buffer32_alloc(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (argc < 1) {
        ps_vm_throw_type_error(vm, "Buffer32.alloc expects (length)");
        return ps_value_undefined();
    }
    size_t length = 0;
    if (!ps_buffer32_parse_length(vm, argv[0], &length)) return ps_value_undefined();
    PSObject *obj = ps_buffer32_new(vm, length);
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

static PSValue ps_native_buffer32_size(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (argc < 1) {
        ps_vm_throw_type_error(vm, "Buffer32.size expects (buffer32)");
        return ps_value_undefined();
    }
    if (argv[0].type != PS_T_OBJECT || !argv[0].as.object) {
        ps_vm_throw_type_error(vm, "Buffer32.size expects (buffer32)");
        return ps_value_undefined();
    }
    PSBuffer32 *buf = ps_buffer32_from_object(argv[0].as.object);
    if (!buf) {
        ps_vm_throw_type_error(vm, "Buffer32.size expects (buffer32)");
        return ps_value_undefined();
    }
    return ps_value_number((double)buf->length);
}

static PSValue ps_native_buffer32_bytelength(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (argc < 1) {
        ps_vm_throw_type_error(vm, "Buffer32.byteLength expects (buffer32)");
        return ps_value_undefined();
    }
    if (argv[0].type != PS_T_OBJECT || !argv[0].as.object) {
        ps_vm_throw_type_error(vm, "Buffer32.byteLength expects (buffer32)");
        return ps_value_undefined();
    }
    PSBuffer32 *buf = ps_buffer32_from_object(argv[0].as.object);
    if (!buf) {
        ps_vm_throw_type_error(vm, "Buffer32.byteLength expects (buffer32)");
        return ps_value_undefined();
    }
    return ps_value_number((double)(buf->length * 4u));
}

static PSValue ps_native_buffer32_view(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (argc < 1) {
        ps_vm_throw_type_error(vm, "Buffer32.view expects (buffer, offset?, length?)");
        return ps_value_undefined();
    }
    if (argv[0].type != PS_T_OBJECT || !argv[0].as.object) {
        ps_vm_throw_type_error(vm, "Buffer32.view expects (buffer, offset?, length?)");
        return ps_value_undefined();
    }
    PSBuffer *buf = ps_buffer_from_object(argv[0].as.object);
    if (!buf) {
        ps_vm_throw_type_error(vm, "Buffer32.view expects (buffer, offset?, length?)");
        return ps_value_undefined();
    }

    size_t offset = 0;
    size_t length = buf->size / 4u;
    if (argc >= 2) {
        if (!ps_buffer_parse_size(vm, argv[1], &offset)) return ps_value_undefined();
    }
    if (offset > SIZE_MAX / 4u) {
        buffer_throw(vm, "RangeError", "Invalid buffer32 offset");
        return ps_value_undefined();
    }
    size_t offset_bytes = offset * 4u;
    if (offset_bytes > buf->size) {
        buffer_throw(vm, "RangeError", "Invalid buffer32 view");
        return ps_value_undefined();
    }
    if (argc >= 3) {
        if (!ps_buffer32_parse_length(vm, argv[2], &length)) return ps_value_undefined();
    } else {
        length = (buf->size - offset_bytes) / 4u;
    }
    if (length > (buf->size - offset_bytes) / 4u) {
        buffer_throw(vm, "RangeError", "Invalid buffer32 view");
        return ps_value_undefined();
    }

    PSObject *obj = ps_buffer32_view(vm, argv[0].as.object, offset_bytes, length);
    if (!obj) return ps_value_undefined();
    return ps_value_object(obj);
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
    PSObject *buffer32 = ps_object_new(NULL);
    if (!buffer) return;
    if (!buffer32) return;

    PSObject *alloc_fn = ps_function_new_native(ps_native_buffer_alloc);
    PSObject *size_fn = ps_function_new_native(ps_native_buffer_size);
    PSObject *slice_fn = ps_function_new_native(ps_native_buffer_slice);
    PSObject *alloc32_fn = ps_function_new_native(ps_native_buffer32_alloc);
    PSObject *size32_fn = ps_function_new_native(ps_native_buffer32_size);
    PSObject *byte32_fn = ps_function_new_native(ps_native_buffer32_bytelength);
    PSObject *view32_fn = ps_function_new_native(ps_native_buffer32_view);

    if (alloc_fn) ps_function_setup(alloc_fn, vm->function_proto, vm->object_proto, NULL);
    if (size_fn) ps_function_setup(size_fn, vm->function_proto, vm->object_proto, NULL);
    if (slice_fn) ps_function_setup(slice_fn, vm->function_proto, vm->object_proto, NULL);
    if (alloc32_fn) ps_function_setup(alloc32_fn, vm->function_proto, vm->object_proto, NULL);
    if (size32_fn) ps_function_setup(size32_fn, vm->function_proto, vm->object_proto, NULL);
    if (byte32_fn) ps_function_setup(byte32_fn, vm->function_proto, vm->object_proto, NULL);
    if (view32_fn) ps_function_setup(view32_fn, vm->function_proto, vm->object_proto, NULL);

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

    if (alloc32_fn) {
        ps_object_define(buffer32, ps_string_from_cstr("alloc"), ps_value_object(alloc32_fn), PS_ATTR_NONE);
    }
    if (size32_fn) {
        ps_object_define(buffer32, ps_string_from_cstr("size"), ps_value_object(size32_fn), PS_ATTR_NONE);
    }
    if (byte32_fn) {
        ps_object_define(buffer32, ps_string_from_cstr("byteLength"), ps_value_object(byte32_fn), PS_ATTR_NONE);
    }
    if (view32_fn) {
        ps_object_define(buffer32, ps_string_from_cstr("view"), ps_value_object(view32_fn), PS_ATTR_NONE);
    }
    ps_object_define(vm->global,
                     ps_string_from_cstr("Buffer32"),
                     ps_value_object(buffer32),
                     PS_ATTR_NONE);
}
