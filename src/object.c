#include "ps_object.h"
#include "ps_gc.h"
#include "ps_config.h"
#include "ps_vm.h"

#include <stdlib.h>
#include <string.h>

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
    return find_prop_const(obj, name) != NULL;
}

PSValue ps_object_get_own(const PSObject *obj, const PSString *name, int *found) {
    const PSProperty *p = find_prop_const(obj, name);
    if (p) {
        if (found) *found = 1;
        return p->value;
    }
    if (found) *found = 0;
    return ps_value_undefined();
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
    const PSObject *cur = obj;
    while (cur) {
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
        return 1;
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

    return 1;
}

int ps_object_put(PSObject *obj, PSString *name, PSValue value) {
    if (!obj || !name) return 0;

    PSProperty *p = find_prop(obj, name);
    if (p) {
        if (p->attrs & PS_ATTR_READONLY) {
            return 0;
        }
        p->value = value;
        obj->cache_name = p->name;
        obj->cache_prop = p;
        return 1;
    }

    /* Default attributes: enumerable, writable, deletable */
    return ps_object_define(obj, name, value, PS_ATTR_NONE);
}

/* --------------------------------------------------------- */
/* Delete                                                    */
/* --------------------------------------------------------- */

int ps_object_delete(PSObject *obj, const PSString *name, int *deleted) {
    if (deleted) *deleted = 0;
    if (!obj || !name) return 0;

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

            if (obj->cache_prop == p) {
                obj->cache_prop = NULL;
                obj->cache_name = NULL;
            }
            free(p);
            if (deleted) *deleted = 1;
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

    for (const PSProperty *p = obj->props; p; p = p->next) {
        if (p->attrs & PS_ATTR_DONTENUM) continue;

        int rc = cb(p->name, p->value, p->attrs, user);
        if (rc != 0) return rc; /* abort requested by callback */
    }
    return 0;
}
