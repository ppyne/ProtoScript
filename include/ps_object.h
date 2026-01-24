#ifndef PS_OBJECT_H
#define PS_OBJECT_H

#include <stddef.h>
#include <stdint.h>

#include "ps_value.h"
#include "ps_string.h"

/* Property attributes (ES1-style) */
typedef enum {
    PS_ATTR_NONE       = 0,
    PS_ATTR_READONLY   = 1 << 0,
    PS_ATTR_DONTENUM   = 1 << 1,
    PS_ATTR_DONTDELETE = 1 << 2
} PSPropAttr;

typedef struct PSProperty {
    PSString *name;          /* property key (immutable string) */
    PSValue   value;         /* property value */
    uint8_t   attrs;         /* PSPropAttr bitmask */
    struct PSProperty *next; /* linked list */
} PSProperty;

typedef struct PSObject {
    struct PSObject *prototype; /* [[Prototype]] */
    PSProperty      *props;     /* own properties */
    PSString        *cache_name; /* last property lookup key */
    PSProperty      *cache_prop; /* last property lookup result */
    int              kind;      /* internal type tag */
    void            *internal;  /* internal data */
} PSObject;

typedef enum {
    PS_OBJ_KIND_PLAIN = 0,
    PS_OBJ_KIND_FUNCTION = 1,
    PS_OBJ_KIND_BOOLEAN = 2,
    PS_OBJ_KIND_NUMBER = 3,
    PS_OBJ_KIND_STRING = 4,
    PS_OBJ_KIND_ARRAY = 5,
    PS_OBJ_KIND_DATE = 6,
    PS_OBJ_KIND_REGEXP = 7,
    PS_OBJ_KIND_BUFFER = 8,
    PS_OBJ_KIND_IMAGE = 9
} PSObjectKind;

/* Object lifecycle */
PSObject *ps_object_new(PSObject *prototype);
void      ps_object_free(PSObject *obj);

/* Property lookup */
int       ps_object_has_own(const PSObject *obj, const PSString *name);
PSValue   ps_object_get_own(const PSObject *obj, const PSString *name, int *found);

/* Prototype chain lookup */
int       ps_object_has(const PSObject *obj, const PSString *name);
PSValue   ps_object_get(const PSObject *obj, const PSString *name, int *found);

/* Define / update */
int ps_object_define(PSObject *obj, PSString *name, PSValue value, uint8_t attrs);
/* Put semantics: update if exists and not READONLY; otherwise define with default attrs */
int ps_object_put(PSObject *obj, PSString *name, PSValue value);

/* Delete */
int ps_object_delete(PSObject *obj, const PSString *name, int *deleted);

/* Enumeration support */
typedef int (*PSPropEnumCallback)(PSString *name, PSValue value, uint8_t attrs, void *user);
/* Calls cb for each enumerable own property. Returns 0 on success, non-zero if cb aborts. */
int ps_object_enum_own(const PSObject *obj, PSPropEnumCallback cb, void *user);

#endif /* PS_OBJECT_H */
