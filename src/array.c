#include "ps_array.h"
#include "ps_gc.h"
#include "ps_vm.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static PSArray *ps_array_ensure(PSObject *obj) {
    if (!obj || obj->kind != PS_OBJ_KIND_ARRAY) return NULL;
    PSArray *arr = (PSArray *)obj->internal;
    if (arr) return arr;
    if (!ps_array_init(obj)) return NULL;
    return (PSArray *)obj->internal;
}

PSArray *ps_array_from_object(PSObject *obj) {
    if (!obj || obj->kind != PS_OBJ_KIND_ARRAY) return NULL;
    return (PSArray *)obj->internal;
}

int ps_array_init(PSObject *obj) {
    if (!obj || obj->kind != PS_OBJ_KIND_ARRAY) return 0;
    if (obj->internal) return 1;
    PSArray *arr = (PSArray *)calloc(1, sizeof(PSArray));
    if (!arr) return 0;
    arr->items = NULL;
    arr->present = NULL;
    arr->length = 0;
    arr->capacity = 0;
    arr->dense = 1;
    obj->internal = arr;
    return 1;
}

void ps_array_free(PSArray *arr) {
    if (!arr) return;
    free(arr->items);
    free(arr->present);
    free(arr);
}

size_t ps_array_length(const PSObject *obj) {
    if (!obj || obj->kind != PS_OBJ_KIND_ARRAY) return 0;
    PSArray *arr = (PSArray *)obj->internal;
    return arr ? arr->length : 0;
}

static int ps_array_grow(PSArray *arr, size_t new_cap) {
    if (!arr) return 0;
    PSValue *items = (PSValue *)realloc(arr->items, new_cap * sizeof(PSValue));
    if (!items && new_cap > 0) return 0;
    size_t old_cap = arr->capacity;
    arr->items = items;
    if (arr->present) {
        uint8_t *present = (uint8_t *)realloc(arr->present, new_cap * sizeof(uint8_t));
        if (!present && new_cap > 0) {
            free(items);
            return 0;
        }
        arr->present = present;
    }
    arr->capacity = new_cap;
    for (size_t i = old_cap; i < new_cap; i++) {
        arr->items[i] = ps_value_undefined();
        if (arr->present) {
            arr->present[i] = 0;
        }
    }
    return 1;
}

static int ps_array_ensure_present(PSArray *arr, int mark_existing) {
    if (!arr) return 0;
    if (arr->present) return 1;
    if (arr->capacity == 0) {
        arr->dense = 0;
        return 1;
    }
    uint8_t *present = (uint8_t *)calloc(arr->capacity, sizeof(uint8_t));
    if (!present) return 0;
    if (mark_existing) {
        size_t limit = arr->length < arr->capacity ? arr->length : arr->capacity;
        for (size_t i = 0; i < limit; i++) {
            present[i] = 1;
        }
    }
    arr->present = present;
    arr->dense = 0;
    return 1;
}

int ps_array_get_index(const PSObject *obj, size_t index, PSValue *out) {
    if (!obj || obj->kind != PS_OBJ_KIND_ARRAY) return 0;
#if PS_ENABLE_PERF
    PSVM *vm = ps_gc_active_vm();
    if (vm) vm->perf.array_get++;
#endif
    PSArray *arr = (PSArray *)obj->internal;
    if (!arr || index >= arr->length || index >= arr->capacity) return 0;
    if (!arr->dense && (!arr->present || !arr->present[index])) return 0;
    if (out) *out = arr->items[index];
    return 1;
}

int ps_array_set_index(PSObject *obj, size_t index, PSValue value) {
    if (!obj || obj->kind != PS_OBJ_KIND_ARRAY) return 0;
#if PS_ENABLE_PERF
    PSVM *vm = ps_gc_active_vm();
    if (vm) vm->perf.array_set++;
#endif
    PSArray *arr = ps_array_ensure(obj);
    if (!arr) return 0;
    if (index >= arr->capacity) {
        size_t new_cap = arr->capacity ? arr->capacity : 8;
        while (new_cap <= index) {
            new_cap *= 2;
        }
        if (!ps_array_grow(arr, new_cap)) return 0;
    }
    if (arr->dense && index > arr->length) {
        if (!ps_array_ensure_present(arr, 1)) return 0;
    }
    if (!arr->dense && !arr->present) {
        if (!ps_array_ensure_present(arr, 0)) return 0;
    }
    arr->items[index] = value;
    if (!arr->dense && arr->present) {
        arr->present[index] = 1;
    }
    if (index >= arr->length) {
        arr->length = index + 1;
    }
    return 1;
}

int ps_array_delete_index(PSObject *obj, size_t index) {
    if (!obj || obj->kind != PS_OBJ_KIND_ARRAY) return 0;
#if PS_ENABLE_PERF
    PSVM *vm = ps_gc_active_vm();
    if (vm) vm->perf.array_delete++;
#endif
    PSArray *arr = (PSArray *)obj->internal;
    if (!arr || index >= arr->capacity) return 0;
    if (arr->dense) {
        if (!ps_array_ensure_present(arr, 1)) return 0;
    }
    if (!arr->present || !arr->present[index]) return 0;
    arr->present[index] = 0;
    arr->items[index] = ps_value_undefined();
    return 1;
}

int ps_array_set_length_internal(PSObject *obj, size_t new_len) {
    if (!obj || obj->kind != PS_OBJ_KIND_ARRAY) return 0;
    PSArray *arr = ps_array_ensure(obj);
    if (!arr) return 0;
    size_t old_len = arr->length;
    if (new_len < old_len) {
        size_t limit = arr->capacity < old_len ? arr->capacity : old_len;
        size_t stop = new_len < limit ? new_len : limit;
        for (size_t i = stop; i < limit; i++) {
            arr->items[i] = ps_value_undefined();
            if (!arr->dense && arr->present) {
                arr->present[i] = 0;
            }
        }
    }
    if (new_len > old_len) {
        if (arr->dense) {
            if (!ps_array_ensure_present(arr, 1)) return 0;
        }
        if (new_len > arr->capacity) {
            if (!ps_array_grow(arr, new_len)) return 0;
        }
    }
    arr->length = new_len;
    return 1;
}

void ps_array_sync_length_prop(PSObject *obj) {
    /* length is tracked internally; array length is virtual in object.c */
    (void)obj;
}

int ps_array_string_to_index(PSString *name, size_t *out_index) {
    if (!name || name->byte_len == 0 || !name->utf8) return 0;
    if (name->index_state == 1) {
        if (name->index_value < 4294967295ULL) {
            if (out_index) *out_index = name->index_value;
            return 1;
        }
        return 0;
    }
    if (name->index_state == 2) return 0;
    const unsigned char *p = (const unsigned char *)name->utf8;
    if (name->byte_len > 1 && p[0] == '0') {
        name->index_state = 2;
        return 0;
    }
    unsigned long long value = 0;
    for (size_t i = 0; i < name->byte_len; i++) {
        if (p[i] < '0' || p[i] > '9') {
            name->index_state = 2;
            return 0;
        }
        value = value * 10ULL + (unsigned long long)(p[i] - '0');
        if (value >= 4294967295ULL) {
            name->index_state = 2;
            return 0;
        }
    }
    name->index_state = 1;
    name->index_value = (size_t)value;
    if (out_index) *out_index = (size_t)value;
    return 1;
}

PSString *ps_array_index_string(struct PSVM *vm, size_t index) {
    if (vm && vm->index_cache && index < vm->index_cache_size) {
        PSString *cached = vm->index_cache[index];
        if (cached) return cached;
        char buf[32];
        snprintf(buf, sizeof(buf), "%zu", index);
        cached = ps_string_from_cstr(buf);
        vm->index_cache[index] = cached;
        return cached;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%zu", index);
    return ps_string_from_cstr(buf);
}
