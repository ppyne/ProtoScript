#ifndef PS_NUMERIC_MAP_H
#define PS_NUMERIC_MAP_H

#include <stddef.h>
#include <stdint.h>

#include "ps_object.h"
#include "ps_value.h"

typedef struct PSNumMap {
    PSValue *items;
    uint8_t *present;
    size_t capacity;
    uint32_t *hash_keys;
    PSValue *hash_values;
    uint8_t *hash_state;
    size_t hash_cap;
    size_t hash_count;
    size_t hash_used;
} PSNumMap;

#define PS_NUM_MAP_MAX_INDEX 65535u

PSNumMap *ps_num_map_from_object(PSObject *obj);
int ps_num_map_init(PSObject *obj);
void ps_num_map_free(PSNumMap *map);

int ps_num_map_get(PSObject *obj, size_t index, PSValue *out);
int ps_num_map_set(PSObject *obj, size_t index, PSValue value, int *is_new);
int ps_num_map_delete(PSObject *obj, size_t index, int *deleted);

#endif /* PS_NUMERIC_MAP_H */
