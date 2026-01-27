#include "ps_object.h"
#include "ps_array.h"
#include "ps_numeric_map.h"
#include "ps_gc.h"
#include "ps_config.h"
#include "ps_vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* --------------------------------------------------------- */
/* Internal helpers                                          */
/* --------------------------------------------------------- */

static int ps_string_equals(const PSString *a, const PSString *b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->hash != b->hash) return 0;
    if (a->byte_len != b->byte_len) return 0;
    return memcmp(a->utf8, b->utf8, a->byte_len) == 0;
}

static int ps_string_is_length(const PSString *s) {
    static const char *length_str = "length";
    if (!s || s->byte_len != 6) return 0;
    return memcmp(s->utf8, length_str, 6) == 0;
}

static void ps_math_intrinsics_invalidate(PSObject *obj) {
    PSVM *vm = ps_gc_active_vm();
    if (vm && obj == vm->math_obj && vm->math_intrinsics_valid) {
        vm->math_intrinsics_valid = 0;
    }
}

static int ps_string_to_k_index(const PSString *name, uint32_t *out_index) {
    if (!name || name->byte_len < 2 || !name->utf8) return 0;
    if (name->utf8[0] != 'k') return 0;
    const unsigned char *p = (const unsigned char *)name->utf8 + 1;
    size_t len = name->byte_len - 1;
    if (len > 1 && p[0] == '0') return 0;
    unsigned long long value = 0;
    for (size_t i = 0; i < len; i++) {
        if (p[i] < '0' || p[i] > '9') return 0;
        value = value * 10ULL + (unsigned long long)(p[i] - '0');
        if (value > UINT32_MAX) return 0;
    }
    if (out_index) *out_index = (uint32_t)value;
    return 1;
}

static uint32_t ps_shape_next_id(void) {
    static uint32_t counter = 1;
    uint32_t next = counter++;
    if (next == 0) {
        next = counter++;
    }
    return next;
}

void ps_object_bump_shape(PSObject *obj) {
    if (!obj) return;
    obj->shape_id = ps_shape_next_id();
}

static size_t ps_object_bucket_index(const PSObject *obj, const PSString *name) {
    return (size_t)name->hash & (obj->bucket_count - 1);
}

static void ps_object_rehash(PSObject *obj, size_t new_count) {
    if (!obj || new_count == 0) return;
    PSProperty **next = (PSProperty **)calloc(new_count, sizeof(PSProperty *));
    if (!next) return;
    size_t guard = 0;
    size_t max_props = obj->prop_count ? obj->prop_count + 1024 : 65536;
    for (PSProperty *p = obj->props; p; p = p->next) {
        if (guard++ > max_props) {
            free(next);
            free(obj->buckets);
            obj->buckets = NULL;
            obj->bucket_count = 0;
            return;
        }
        size_t idx = (size_t)p->name->hash & (new_count - 1);
        p->hash_next = next[idx];
        next[idx] = p;
    }
    free(obj->buckets);
    obj->buckets = next;
    obj->bucket_count = new_count;
}

static void ps_object_ensure_buckets(PSObject *obj) {
    if (!obj || obj->buckets) return;
    size_t count = 64;
    ps_object_rehash(obj, count);
}

static PSProperty *find_prop(PSObject *obj, const PSString *name) {
    if (!obj || !name) return NULL;
    if (obj->cache_prop && obj->cache_name && ps_string_equals(obj->cache_name, name)) {
        return obj->cache_prop;
    }
    if (obj->buckets && obj->bucket_count > 0) {
        size_t idx = ps_object_bucket_index(obj, name);
        size_t guard = 0;
        size_t max_props = obj->prop_count ? obj->prop_count + 1024 : 65536;
        for (PSProperty *p = obj->buckets[idx]; p; p = p->hash_next) {
            if (guard++ > max_props) {
                free(obj->buckets);
                obj->buckets = NULL;
                obj->bucket_count = 0;
                break;
            }
            if (ps_string_equals(p->name, name)) {
                obj->cache_name = p->name;
                obj->cache_prop = p;
                return p;
            }
        }
        if (!obj->buckets || obj->bucket_count == 0) {
            /* fallback to linear scan if buckets are disabled */
        } else {
            return NULL;
        }
    }
    for (PSProperty *p = obj->props; p; p = p->next) {
        if (ps_string_equals(p->name, name)) {
            obj->cache_name = p->name;
            obj->cache_prop = p;
            return p;
        }
    }
    return NULL;
}

static const PSProperty *find_prop_const(const PSObject *obj, const PSString *name) {
    return find_prop((PSObject *)obj, name);
}

/* --------------------------------------------------------- */
/* Object lifecycle                                          */
/* --------------------------------------------------------- */

PSObject *ps_object_new(PSObject *prototype) {
    PSObject *o = (PSObject *)ps_gc_alloc(PS_GC_OBJECT, sizeof(PSObject));
    if (!o) return NULL;
#if PS_ENABLE_PERF
    PSVM *vm = ps_gc_active_vm();
    if (vm) {
        vm->perf.object_new++;
    }
#endif
    o->prototype = prototype;
    o->props = NULL;
    o->cache_name = NULL;
    o->cache_prop = NULL;
    o->buckets = NULL;
    o->bucket_count = 0;
    o->prop_count = 0;
    o->kind = PS_OBJ_KIND_PLAIN;
    o->internal = NULL;
    o->shape_id = ps_shape_next_id();
    o->internal_kind = PS_INTERNAL_NONE;
    return o;
}

void ps_object_free(PSObject *obj) {
    if (!obj) return;
    if (ps_gc_is_managed(obj)) {
        ps_gc_free(obj);
        return;
    }

    PSProperty *p = obj->props;
    while (p) {
        PSProperty *next = p->next;
        /* We do NOT free p->name or p->value payloads here.
           Those will be owned/managed by the GC later. */
        free(p);
        p = next;
    }
    free(obj->buckets);
    free(obj);
}

/* --------------------------------------------------------- */
/* Property lookup (own)                                     */
/* --------------------------------------------------------- */

int ps_object_has_own(const PSObject *obj, const PSString *name) {
#if PS_ENABLE_PERF
    PSVM *vm = ps_gc_active_vm();
    if (vm) vm->perf.object_get++;
#endif
    if (obj && obj->kind == PS_OBJ_KIND_ARRAY && ps_string_is_length(name)) {
        return 1;
    }
    if (obj && obj->kind == PS_OBJ_KIND_PLAIN && obj->internal_kind == PS_INTERNAL_NUMMAP) {
        size_t index = 0;
        if (ps_array_string_to_index((PSString *)name, &index)) {
            PSValue out = ps_value_undefined();
            if (ps_num_map_get((PSObject *)obj, index, &out)) return 1;
        }
        uint32_t kindex = 0;
        if (ps_string_to_k_index(name, &kindex)) {
            PSValue out = ps_value_undefined();
            if (ps_num_map_k_get((PSObject *)obj, kindex, &out)) return 1;
        }
    }
    if (obj && obj->kind == PS_OBJ_KIND_ARRAY) {
        size_t index = 0;
        if (ps_array_string_to_index((PSString *)name, &index)) {
            PSValue out = ps_value_undefined();
            if (ps_array_get_index(obj, index, &out)) return 1;
        }
    }
    return find_prop_const(obj, name) != NULL;
}

PSValue ps_object_get_own(const PSObject *obj, const PSString *name, int *found) {
#if PS_ENABLE_PERF
    PSVM *vm = ps_gc_active_vm();
    if (vm) vm->perf.object_get++;
#endif
    if (obj && obj->kind == PS_OBJ_KIND_ARRAY && ps_string_is_length(name)) {
        if (found) *found = 1;
        return ps_value_number((double)ps_array_length(obj));
    }
    if (obj && obj->kind == PS_OBJ_KIND_PLAIN && obj->internal_kind == PS_INTERNAL_NUMMAP) {
        size_t index = 0;
        if (ps_array_string_to_index((PSString *)name, &index)) {
            PSValue out = ps_value_undefined();
            if (ps_num_map_get((PSObject *)obj, index, &out)) {
                if (found) *found = 1;
                return out;
            }
        }
        uint32_t kindex = 0;
        if (ps_string_to_k_index(name, &kindex)) {
            PSValue out = ps_value_undefined();
            if (ps_num_map_k_get((PSObject *)obj, kindex, &out)) {
                if (found) *found = 1;
                return out;
            }
        }
    }
    if (obj && obj->kind == PS_OBJ_KIND_ARRAY) {
        size_t index = 0;
        if (ps_array_string_to_index((PSString *)name, &index)) {
            PSValue out = ps_value_undefined();
            if (ps_array_get_index(obj, index, &out)) {
                if (found) *found = 1;
                return out;
            }
        }
    }
    const PSProperty *p = find_prop_const(obj, name);
    if (p) {
        if (found) *found = 1;
        return p->value;
    }
    if (found) *found = 0;
    return ps_value_undefined();
}

PSProperty *ps_object_get_own_prop(PSObject *obj, const PSString *name) {
    return find_prop(obj, name);
}

/* --------------------------------------------------------- */
/* Prototype chain lookup                                    */
/* --------------------------------------------------------- */

int ps_object_has(const PSObject *obj, const PSString *name) {
    int f = 0;
    (void)ps_object_get(obj, name, &f);
    return f;
}

PSValue ps_object_get(const PSObject *obj, const PSString *name, int *found) {
#if PS_ENABLE_PERF
    PSVM *vm = ps_gc_active_vm();
    if (vm) vm->perf.object_get++;
#endif
    if (obj && obj->kind == PS_OBJ_KIND_ARRAY && ps_string_is_length(name)) {
        if (found) *found = 1;
        return ps_value_number((double)ps_array_length(obj));
    }
    const PSObject *cur = obj;
    while (cur) {
        if (cur->kind == PS_OBJ_KIND_PLAIN && cur->internal_kind == PS_INTERNAL_NUMMAP) {
            size_t index = 0;
            if (ps_array_string_to_index((PSString *)name, &index)) {
                PSValue out = ps_value_undefined();
                if (ps_num_map_get((PSObject *)cur, index, &out)) {
                    if (found) *found = 1;
                    return out;
                }
            }
            uint32_t kindex = 0;
            if (ps_string_to_k_index(name, &kindex)) {
                PSValue out = ps_value_undefined();
                if (ps_num_map_k_get((PSObject *)cur, kindex, &out)) {
                    if (found) *found = 1;
                    return out;
                }
            }
        }
        if (cur->kind == PS_OBJ_KIND_ARRAY) {
            size_t index = 0;
            if (ps_array_string_to_index((PSString *)name, &index)) {
                PSValue out = ps_value_undefined();
                if (ps_array_get_index(cur, index, &out)) {
                    if (found) *found = 1;
                    return out;
                }
            }
        }
        const PSProperty *p = find_prop_const(cur, name);
        if (p) {
            if (found) *found = 1;
            return p->value;
        }
        cur = cur->prototype;
    }
    if (found) *found = 0;
    return ps_value_undefined();
}

/* --------------------------------------------------------- */
/* Define / update                                           */
/* --------------------------------------------------------- */

int ps_object_define(PSObject *obj, PSString *name, PSValue value, uint8_t attrs) {
    if (!obj || !name) return 0;

#if PS_ENABLE_PERF
    PSVM *vm = ps_gc_active_vm();
    if (vm) vm->perf.object_define++;
#endif

    PSProperty *p = find_prop(obj, name);
    if (p) {
        /* Re-define semantics:
           - if READONLY: value cannot change
           - attributes are replaced as requested (simple rule for now) */
        if (p->attrs & PS_ATTR_READONLY) {
            return 0;
        }
        p->value = value;
        p->attrs = attrs;
        obj->cache_name = p->name;
        obj->cache_prop = p;
        ps_math_intrinsics_invalidate(obj);
        return 1;
    }

    if (obj->kind == PS_OBJ_KIND_PLAIN &&
        attrs == PS_ATTR_NONE &&
        (obj->internal == NULL || obj->internal_kind == PS_INTERNAL_NUMMAP)) {
        size_t index = 0;
        if (ps_array_string_to_index((PSString *)name, &index)) {
            int is_new = 0;
            if (!ps_num_map_set(obj, index, value, &is_new)) return 0;
            if (is_new) ps_object_bump_shape(obj);
            ps_math_intrinsics_invalidate(obj);
            return 1;
        }
        uint32_t kindex = 0;
        if (ps_string_to_k_index(name, &kindex)) {
            int is_new = 0;
            if (!ps_num_map_k_set(obj, kindex, value, &is_new)) return 0;
            if (is_new) ps_object_bump_shape(obj);
            ps_math_intrinsics_invalidate(obj);
            return 1;
        }
    }

    if (obj->kind == PS_OBJ_KIND_ARRAY && attrs == PS_ATTR_NONE) {
        size_t index = 0;
        if (ps_array_string_to_index((PSString *)name, &index)) {
            if (!ps_array_set_index(obj, index, value)) return 0;
            ps_math_intrinsics_invalidate(obj);
            return 1;
        }
    }

    p = (PSProperty *)calloc(1, sizeof(PSProperty));
    if (!p) return 0;

    p->name  = name;
    p->value = value;
    p->attrs = attrs;
    p->hash_next = NULL;

    /* Insert at head (stable enough for now; order is implementation-defined) */
    p->next = obj->props;
    obj->props = p;
    obj->prop_count++;
    ps_object_bump_shape(obj);

    if (!obj->buckets && obj->prop_count > 8) {
        ps_object_ensure_buckets(obj);
    }
    if (obj->buckets && obj->bucket_count > 0) {
        size_t idx = ps_object_bucket_index(obj, name);
        p->hash_next = obj->buckets[idx];
        obj->buckets[idx] = p;
        if (obj->prop_count > obj->bucket_count * 2) {
            ps_object_rehash(obj, obj->bucket_count * 2);
        }
    }
    obj->cache_name = p->name;
    obj->cache_prop = p;
    ps_math_intrinsics_invalidate(obj);

    return 1;
}

int ps_object_put(PSObject *obj, PSString *name, PSValue value) {
    if (!obj || !name) return 0;

#if PS_ENABLE_PERF
    PSVM *vm = ps_gc_active_vm();
    if (vm) vm->perf.object_put++;
#endif

    PSProperty *p = find_prop(obj, name);
    if (p) {
        if (p->attrs & PS_ATTR_READONLY) {
            return 0;
        }
        p->value = value;
        obj->cache_name = p->name;
        obj->cache_prop = p;
        ps_math_intrinsics_invalidate(obj);
        return 1;
    }

    if (obj->kind == PS_OBJ_KIND_PLAIN &&
        (obj->internal == NULL || obj->internal_kind == PS_INTERNAL_NUMMAP)) {
        size_t index = 0;
        if (ps_array_string_to_index((PSString *)name, &index)) {
            int is_new = 0;
            if (!ps_num_map_set(obj, index, value, &is_new)) return 0;
            if (is_new) ps_object_bump_shape(obj);
            ps_math_intrinsics_invalidate(obj);
            return 1;
        }
        uint32_t kindex = 0;
        if (ps_string_to_k_index(name, &kindex)) {
            int is_new = 0;
            if (!ps_num_map_k_set(obj, kindex, value, &is_new)) return 0;
            if (is_new) ps_object_bump_shape(obj);
            ps_math_intrinsics_invalidate(obj);
            return 1;
        }
    }

    if (obj->kind == PS_OBJ_KIND_ARRAY) {
        size_t index = 0;
        if (ps_array_string_to_index((PSString *)name, &index)) {
            int ok = ps_array_set_index(obj, index, value);
            if (ok) {
                ps_math_intrinsics_invalidate(obj);
            }
            return ok;
        }
    }

    /* Default attributes: enumerable, writable, deletable */
    {
        int ok = ps_object_define(obj, name, value, PS_ATTR_NONE);
        if (ok) {
            ps_math_intrinsics_invalidate(obj);
        }
        return ok;
    }
}

/* --------------------------------------------------------- */
/* Delete                                                    */
/* --------------------------------------------------------- */

int ps_object_delete(PSObject *obj, const PSString *name, int *deleted) {
    if (deleted) *deleted = 0;
    if (!obj || !name) return 0;

#if PS_ENABLE_PERF
    PSVM *vm = ps_gc_active_vm();
    if (vm) vm->perf.object_delete++;
#endif

    if (obj->kind == PS_OBJ_KIND_PLAIN && obj->internal_kind == PS_INTERNAL_NUMMAP) {
        size_t index = 0;
        if (ps_array_string_to_index((PSString *)name, &index)) {
            if (!find_prop(obj, name)) {
                int did_delete = 0;
                if (ps_num_map_delete(obj, index, &did_delete) && did_delete) {
                    ps_object_bump_shape(obj);
                }
                if (deleted) *deleted = did_delete ? 1 : 0;
                return 1;
            }
        }
        uint32_t kindex = 0;
        if (ps_string_to_k_index(name, &kindex)) {
            if (!find_prop(obj, name)) {
                int did_delete = 0;
                if (ps_num_map_k_delete(obj, kindex, &did_delete) && did_delete) {
                    ps_object_bump_shape(obj);
                }
                if (deleted) *deleted = did_delete ? 1 : 0;
                return 1;
            }
        }
    }

    if (obj->kind == PS_OBJ_KIND_ARRAY) {
        size_t index = 0;
        if (ps_array_string_to_index((PSString *)name, &index)) {
            if (!find_prop(obj, name)) {
                int did_delete = ps_array_delete_index(obj, index);
                if (deleted) *deleted = did_delete ? 1 : 0;
                return 1;
            }
        }
    }

    PSProperty *prev = NULL;
    PSProperty *p = obj->props;

    while (p) {
        if (ps_string_equals(p->name, name)) {
            if (p->attrs & PS_ATTR_DONTDELETE) {
                return 0;
            }

            if (prev) prev->next = p->next;
            else obj->props = p->next;

            if (obj->buckets && obj->bucket_count > 0) {
                size_t idx = ps_object_bucket_index(obj, name);
                PSProperty *hprev = NULL;
                PSProperty *hp = obj->buckets[idx];
                while (hp) {
                    if (hp == p) {
                        if (hprev) hprev->hash_next = hp->hash_next;
                        else obj->buckets[idx] = hp->hash_next;
                        break;
                    }
                    hprev = hp;
                    hp = hp->hash_next;
                }
            }
            if (obj->prop_count > 0) {
                obj->prop_count--;
            }
            ps_object_bump_shape(obj);

            if (obj->cache_prop == p) {
                obj->cache_prop = NULL;
                obj->cache_name = NULL;
            }
            free(p);
            if (deleted) *deleted = 1;
            ps_math_intrinsics_invalidate(obj);
            return 1;
        }
        prev = p;
        p = p->next;
    }

    /* Deleting a non-existing property succeeds (ES-like behavior) */
    return 1;
}

/* --------------------------------------------------------- */
/* Enumeration                                               */
/* --------------------------------------------------------- */

int ps_object_enum_own(const PSObject *obj, PSPropEnumCallback cb, void *user) {
    if (!obj || !cb) return 0;

    if (obj->kind == PS_OBJ_KIND_PLAIN && obj->internal_kind == PS_INTERNAL_NUMMAP) {
        const PSNumMap *map = (const PSNumMap *)obj->internal;
        PSVM *vm = ps_gc_active_vm();
        if (map && map->capacity > 0) {
            for (size_t i = 0; i < map->capacity; i++) {
                if (!map->present[i]) continue;
                PSString *name = ps_array_index_string(vm, i);
                int rc = cb(name, map->items[i], PS_ATTR_NONE, user);
                if (rc != 0) return rc;
            }
        }
        if (map && map->hash_state && map->hash_cap > 0) {
            char buf[32];
            for (size_t i = 0; i < map->hash_cap; i++) {
                if (map->hash_state[i] != 1) continue;
                uint32_t key = map->hash_keys[i];
                if (key <= PS_NUM_MAP_MAX_INDEX) {
                    PSString *name = ps_array_index_string(vm, key);
                    int rc = cb(name, map->hash_values[i], PS_ATTR_NONE, user);
                    if (rc != 0) return rc;
                    continue;
                }
                snprintf(buf, sizeof(buf), "%u", key);
                PSString *name = ps_string_from_cstr(buf);
                int rc = cb(name, map->hash_values[i], PS_ATTR_NONE, user);
                if (rc != 0) return rc;
            }
        }
        if (map && map->k_capacity > 0) {
            char buf[32];
            for (size_t i = 0; i < map->k_capacity; i++) {
                if (!map->k_present[i]) continue;
                snprintf(buf, sizeof(buf), "k%zu", i);
                PSString *name = ps_string_from_cstr(buf);
                int rc = cb(name, map->k_items[i], PS_ATTR_NONE, user);
                if (rc != 0) return rc;
            }
        }
        if (map && map->k_hash_state && map->k_hash_cap > 0) {
            char buf[32];
            for (size_t i = 0; i < map->k_hash_cap; i++) {
                if (map->k_hash_state[i] != 1) continue;
                snprintf(buf, sizeof(buf), "k%u", map->k_hash_keys[i]);
                PSString *name = ps_string_from_cstr(buf);
                int rc = cb(name, map->k_hash_values[i], PS_ATTR_NONE, user);
                if (rc != 0) return rc;
            }
        }
    }

    if (obj->kind == PS_OBJ_KIND_ARRAY && obj->internal) {
        const PSArray *arr = (const PSArray *)obj->internal;
        PSVM *vm = ps_gc_active_vm();
        size_t limit = arr->capacity < arr->length ? arr->capacity : arr->length;
        for (size_t i = 0; i < limit; i++) {
            if (!arr->dense && (!arr->present || !arr->present[i])) continue;
            PSString *name = ps_array_index_string(vm, i);
            int rc = cb(name, arr->items[i], PS_ATTR_NONE, user);
            if (rc != 0) return rc;
        }
    }

    for (const PSProperty *p = obj->props; p; p = p->next) {
        if (p->attrs & PS_ATTR_DONTENUM) continue;
        if (obj->kind == PS_OBJ_KIND_ARRAY && obj->internal) {
            size_t index = 0;
            if (ps_array_string_to_index(p->name, &index)) {
                PSValue out = ps_value_undefined();
                if (ps_array_get_index(obj, index, &out)) {
                    continue;
                }
            }
        }

        int rc = cb(p->name, p->value, p->attrs, user);
        if (rc != 0) return rc; /* abort requested by callback */
    }
    return 0;
}
