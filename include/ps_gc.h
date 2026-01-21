#ifndef PS_GC_H
#define PS_GC_H

#include <stddef.h>
#include <stdint.h>

#include "ps_value.h"

struct PSVM;

typedef enum {
    PS_GC_OBJECT = 1,
    PS_GC_STRING = 2,
    PS_GC_ENV = 3,
    PS_GC_FUNCTION = 4
} PSGCType;

typedef enum {
    PS_GC_ROOT_VALUE = 1,
    PS_GC_ROOT_OBJECT = 2,
    PS_GC_ROOT_STRING = 3,
    PS_GC_ROOT_ENV = 4,
    PS_GC_ROOT_FUNCTION = 5
} PSGCRootType;

typedef struct PSGCHeader {
    uint32_t magic;
    uint8_t  marked;
    uint8_t  type;
    size_t   size;
    struct PSGCHeader *next;
} PSGCHeader;

typedef struct PSGCRoot {
    PSGCRootType type;
    void        *ptr;
} PSGCRoot;

typedef struct PSGC {
    PSGCHeader *head;
    size_t      heap_bytes;
    size_t      live_bytes_last;
    size_t      bytes_since_gc;
    size_t      threshold;
    size_t      min_threshold;
    double      growth_factor;
    int         collections;
    int         freed_last;
    int         should_collect;
    int         in_collect;
    PSGCRoot   *roots;
    size_t      root_count;
    size_t      root_cap;
} PSGC;

void   ps_gc_init(struct PSVM *vm);
void   ps_gc_destroy(struct PSVM *vm);
void   ps_gc_set_active_vm(struct PSVM *vm);
struct PSVM *ps_gc_active_vm(void);
void  *ps_gc_alloc_vm(struct PSVM *vm, PSGCType type, size_t size);
void  *ps_gc_alloc(PSGCType type, size_t size);
void   ps_gc_free(void *ptr);
int    ps_gc_is_managed(const void *ptr);
void   ps_gc_safe_point(struct PSVM *vm);
void   ps_gc_collect(struct PSVM *vm);

void ps_gc_root_push(struct PSVM *vm, PSGCRootType type, void *ptr);
void ps_gc_root_pop(struct PSVM *vm, size_t count);

#endif /* PS_GC_H */
