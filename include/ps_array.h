#ifndef PS_ARRAY_H
#define PS_ARRAY_H

#include <stddef.h>
#include <stdint.h>

#include "ps_object.h"
#include "ps_string.h"
#include "ps_value.h"

struct PSVM;

typedef struct PSArray {
    PSValue *items;
    uint8_t *present;
    size_t length;
    size_t capacity;
} PSArray;

PSArray *ps_array_from_object(PSObject *obj);
int ps_array_init(PSObject *obj);
void ps_array_free(PSArray *arr);

size_t ps_array_length(const PSObject *obj);
int ps_array_get_index(const PSObject *obj, size_t index, PSValue *out);
int ps_array_set_index(PSObject *obj, size_t index, PSValue value);
int ps_array_delete_index(PSObject *obj, size_t index);
int ps_array_set_length_internal(PSObject *obj, size_t new_len);
void ps_array_sync_length_prop(PSObject *obj);

int ps_array_string_to_index(const PSString *name, size_t *out_index);
PSString *ps_array_index_string(struct PSVM *vm, size_t index);

#endif /* PS_ARRAY_H */
