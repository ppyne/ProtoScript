#include "ps_object.h"

#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------- */
/* Internal helpers                                          */
/* --------------------------------------------------------- */

static int ps_string_equals(const PSString *a, const PSString *b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->byte_len != b->byte_len) return 0;
    return memcmp(a->utf8, b->utf8, a->byte_len) == 0;
}

static PSProperty *find_prop(PSObject *obj, const PSString *name) {
    if (!obj || !name) return NULL;
    for (PSProperty *p = obj->props; p; p = p->next) {
        if (ps_string_equals(p->name, name)) return p;
    }
    return NULL;
}

static const PSProperty *find_prop_const(const PSObject *obj, const PSString *name) {
    if (!obj || !name) return NULL;
    for (const PSProperty *p = obj->props; p; p = p->next) {
        if (ps_string_equals(p->name, name)) return p;
    }
    return NULL;
}

/* --------------------------------------------------------- */
/* Object lifecycle                                          */
/* --------------------------------------------------------- */

PSObject *ps_object_new(PSObject *prototype) {
    PSObject *o = (PSObject *)calloc(1, sizeof(PSObject));
    if (!o) return NULL;
    o->prototype = prototype;
    o->props = NULL;
    o->kind = PS_OBJ_KIND_PLAIN;
    o->internal = NULL;
    return o;
}

void ps_object_free(PSObject *obj) {
    if (!obj) return;

    PSProperty *p = obj->props;
    while (p) {
        PSProperty *next = p->next;
        /* We do NOT free p->name or p->value payloads here.
           Those will be owned/managed by the GC later. */
        free(p);
        p = next;
    }
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
        return 1;
    }

    p = (PSProperty *)calloc(1, sizeof(PSProperty));
    if (!p) return 0;

    p->name  = name;
    p->value = value;
    p->attrs = attrs;

    /* Insert at head (stable enough for now; order is implementation-defined) */
    p->next = obj->props;
    obj->props = p;

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
