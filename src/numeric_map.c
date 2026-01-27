#include "ps_numeric_map.h"

#include <stdlib.h>

static PSNumMap *ps_num_map_ensure(PSObject *obj) {
    if (!obj || obj->kind != PS_OBJ_KIND_PLAIN) return NULL;
    if (obj->internal && obj->internal_kind != PS_INTERNAL_NUMMAP) return NULL;
    PSNumMap *map = (PSNumMap *)obj->internal;
    if (map) return map;
    if (!ps_num_map_init(obj)) return NULL;
    return (PSNumMap *)obj->internal;
}

PSNumMap *ps_num_map_from_object(PSObject *obj) {
    if (!obj || obj->kind != PS_OBJ_KIND_PLAIN) return NULL;
    if (obj->internal_kind != PS_INTERNAL_NUMMAP) return NULL;
    return (PSNumMap *)obj->internal;
}

int ps_num_map_init(PSObject *obj) {
    if (!obj || obj->kind != PS_OBJ_KIND_PLAIN) return 0;
    if (obj->internal) {
        return obj->internal_kind == PS_INTERNAL_NUMMAP ? 1 : 0;
    }
    PSNumMap *map = (PSNumMap *)calloc(1, sizeof(PSNumMap));
    if (!map) return 0;
    map->items = NULL;
    map->present = NULL;
    map->capacity = 0;
    map->hash_keys = NULL;
    map->hash_values = NULL;
    map->hash_state = NULL;
    map->hash_cap = 0;
    map->hash_count = 0;
    map->hash_used = 0;
    map->k_items = NULL;
    map->k_present = NULL;
    map->k_capacity = 0;
    map->k_hash_keys = NULL;
    map->k_hash_values = NULL;
    map->k_hash_state = NULL;
    map->k_hash_cap = 0;
    map->k_hash_count = 0;
    map->k_hash_used = 0;
    obj->internal = map;
    obj->internal_kind = PS_INTERNAL_NUMMAP;
    return 1;
}

void ps_num_map_free(PSNumMap *map) {
    if (!map) return;
    free(map->items);
    free(map->present);
    free(map->hash_keys);
    free(map->hash_values);
    free(map->hash_state);
    free(map->k_items);
    free(map->k_present);
    free(map->k_hash_keys);
    free(map->k_hash_values);
    free(map->k_hash_state);
    free(map);
}

static int ps_num_map_grow(PSNumMap *map, size_t new_cap) {
    if (!map) return 0;
    PSValue *items = (PSValue *)realloc(map->items, new_cap * sizeof(PSValue));
    if (!items && new_cap > 0) return 0;
    uint8_t *present = (uint8_t *)realloc(map->present, new_cap * sizeof(uint8_t));
    if (!present && new_cap > 0) {
        free(items);
        return 0;
    }
    size_t old_cap = map->capacity;
    map->items = items;
    map->present = present;
    map->capacity = new_cap;
    for (size_t i = old_cap; i < new_cap; i++) {
        map->items[i] = ps_value_undefined();
        map->present[i] = 0;
    }
    return 1;
}

static uint32_t ps_num_hash_key(uint32_t key) {
    return key * 2654435761u;
}

static int ps_num_map_hash_grow(PSNumMap *map, size_t new_cap) {
    if (!map) return 0;
    if (new_cap < 16) new_cap = 16;
    size_t cap = 1;
    while (cap < new_cap) cap <<= 1;

    uint32_t *keys = (uint32_t *)calloc(cap, sizeof(uint32_t));
    if (!keys) return 0;
    PSValue *values = (PSValue *)calloc(cap, sizeof(PSValue));
    if (!values) {
        free(keys);
        return 0;
    }
    uint8_t *state = (uint8_t *)calloc(cap, sizeof(uint8_t));
    if (!state) {
        free(keys);
        free(values);
        return 0;
    }

    size_t old_cap = map->hash_cap;
    uint32_t *old_keys = map->hash_keys;
    PSValue *old_values = map->hash_values;
    uint8_t *old_state = map->hash_state;

    map->hash_keys = keys;
    map->hash_values = values;
    map->hash_state = state;
    map->hash_cap = cap;
    map->hash_count = 0;
    map->hash_used = 0;

    if (old_cap && old_state) {
        for (size_t i = 0; i < old_cap; i++) {
            if (old_state[i] == 1) {
                uint32_t key = old_keys[i];
                PSValue val = old_values[i];
                uint32_t h = ps_num_hash_key(key);
                size_t idx = (size_t)(h & (cap - 1));
                while (map->hash_state[idx] == 1) {
                    idx = (idx + 1) & (cap - 1);
                }
                map->hash_state[idx] = 1;
                map->hash_keys[idx] = key;
                map->hash_values[idx] = val;
                map->hash_count++;
                map->hash_used++;
            }
        }
    }

    free(old_keys);
    free(old_values);
    free(old_state);
    return 1;
}

static int ps_num_map_hash_get(PSNumMap *map, uint32_t key, PSValue *out) {
    if (!map || map->hash_cap == 0) return 0;
    size_t cap = map->hash_cap;
    size_t idx = (size_t)(ps_num_hash_key(key) & (cap - 1));
    size_t start = idx;
    while (map->hash_state[idx]) {
        if (map->hash_state[idx] == 1 && map->hash_keys[idx] == key) {
            if (out) *out = map->hash_values[idx];
            return 1;
        }
        idx = (idx + 1) & (cap - 1);
        if (idx == start) break;
    }
    return 0;
}

static int ps_num_map_hash_set(PSNumMap *map, uint32_t key, PSValue value, int *is_new) {
    if (is_new) *is_new = 0;
    if (!map) return 0;
    if (map->hash_cap == 0) {
        if (!ps_num_map_hash_grow(map, 16)) return 0;
    } else if ((map->hash_used + 1) * 10 >= map->hash_cap * 7) {
        if (!ps_num_map_hash_grow(map, map->hash_cap * 2)) return 0;
    }

    size_t cap = map->hash_cap;
    size_t idx = (size_t)(ps_num_hash_key(key) & (cap - 1));
    size_t first_tomb = (size_t)-1;
    while (map->hash_state[idx]) {
        if (map->hash_state[idx] == 1 && map->hash_keys[idx] == key) {
            map->hash_values[idx] = value;
            return 1;
        }
        if (first_tomb == (size_t)-1 && map->hash_state[idx] == 2) {
            first_tomb = idx;
        }
        idx = (idx + 1) & (cap - 1);
    }

    if (first_tomb != (size_t)-1) {
        idx = first_tomb;
    } else {
        map->hash_used++;
    }
    map->hash_state[idx] = 1;
    map->hash_keys[idx] = key;
    map->hash_values[idx] = value;
    map->hash_count++;
    if (is_new) *is_new = 1;
    return 1;
}

static int ps_num_map_hash_delete(PSNumMap *map, uint32_t key, int *deleted) {
    if (deleted) *deleted = 0;
    if (!map || map->hash_cap == 0) return 0;
    size_t cap = map->hash_cap;
    size_t idx = (size_t)(ps_num_hash_key(key) & (cap - 1));
    size_t start = idx;
    while (map->hash_state[idx]) {
        if (map->hash_state[idx] == 1 && map->hash_keys[idx] == key) {
            map->hash_state[idx] = 2;
            map->hash_values[idx] = ps_value_undefined();
            map->hash_count--;
            if (deleted) *deleted = 1;
            return 1;
        }
        idx = (idx + 1) & (cap - 1);
        if (idx == start) break;
    }
    return 0;
}

static int ps_num_map_k_grow(PSNumMap *map, size_t new_cap) {
    if (!map) return 0;
    PSValue *items = (PSValue *)realloc(map->k_items, new_cap * sizeof(PSValue));
    if (!items && new_cap > 0) return 0;
    uint8_t *present = (uint8_t *)realloc(map->k_present, new_cap * sizeof(uint8_t));
    if (!present && new_cap > 0) {
        free(items);
        return 0;
    }
    size_t old_cap = map->k_capacity;
    map->k_items = items;
    map->k_present = present;
    map->k_capacity = new_cap;
    for (size_t i = old_cap; i < new_cap; i++) {
        map->k_items[i] = ps_value_undefined();
        map->k_present[i] = 0;
    }
    return 1;
}

static int ps_num_map_k_hash_grow(PSNumMap *map, size_t new_cap) {
    if (!map) return 0;
    if (new_cap < 16) new_cap = 16;
    size_t cap = 1;
    while (cap < new_cap) cap <<= 1;

    uint32_t *keys = (uint32_t *)calloc(cap, sizeof(uint32_t));
    if (!keys) return 0;
    PSValue *values = (PSValue *)calloc(cap, sizeof(PSValue));
    if (!values) {
        free(keys);
        return 0;
    }
    uint8_t *state = (uint8_t *)calloc(cap, sizeof(uint8_t));
    if (!state) {
        free(keys);
        free(values);
        return 0;
    }

    size_t old_cap = map->k_hash_cap;
    uint32_t *old_keys = map->k_hash_keys;
    PSValue *old_values = map->k_hash_values;
    uint8_t *old_state = map->k_hash_state;

    map->k_hash_keys = keys;
    map->k_hash_values = values;
    map->k_hash_state = state;
    map->k_hash_cap = cap;
    map->k_hash_count = 0;
    map->k_hash_used = 0;

    if (old_cap && old_state) {
        for (size_t i = 0; i < old_cap; i++) {
            if (old_state[i] == 1) {
                uint32_t key = old_keys[i];
                PSValue val = old_values[i];
                uint32_t h = ps_num_hash_key(key);
                size_t idx = (size_t)(h & (cap - 1));
                while (map->k_hash_state[idx] == 1) {
                    idx = (idx + 1) & (cap - 1);
                }
                map->k_hash_state[idx] = 1;
                map->k_hash_keys[idx] = key;
                map->k_hash_values[idx] = val;
                map->k_hash_count++;
                map->k_hash_used++;
            }
        }
    }

    free(old_keys);
    free(old_values);
    free(old_state);
    return 1;
}

static int ps_num_map_k_hash_get(PSNumMap *map, uint32_t key, PSValue *out) {
    if (!map || map->k_hash_cap == 0) return 0;
    size_t cap = map->k_hash_cap;
    size_t idx = (size_t)(ps_num_hash_key(key) & (cap - 1));
    size_t start = idx;
    while (map->k_hash_state[idx]) {
        if (map->k_hash_state[idx] == 1 && map->k_hash_keys[idx] == key) {
            if (out) *out = map->k_hash_values[idx];
            return 1;
        }
        idx = (idx + 1) & (cap - 1);
        if (idx == start) break;
    }
    return 0;
}

static int ps_num_map_k_hash_set(PSNumMap *map, uint32_t key, PSValue value, int *is_new) {
    if (is_new) *is_new = 0;
    if (!map) return 0;
    if (map->k_hash_cap == 0) {
        if (!ps_num_map_k_hash_grow(map, 16)) return 0;
    } else if ((map->k_hash_used + 1) * 10 >= map->k_hash_cap * 7) {
        if (!ps_num_map_k_hash_grow(map, map->k_hash_cap * 2)) return 0;
    }

    size_t cap = map->k_hash_cap;
    size_t idx = (size_t)(ps_num_hash_key(key) & (cap - 1));
    size_t first_tomb = (size_t)-1;
    while (map->k_hash_state[idx]) {
        if (map->k_hash_state[idx] == 1 && map->k_hash_keys[idx] == key) {
            map->k_hash_values[idx] = value;
            return 1;
        }
        if (first_tomb == (size_t)-1 && map->k_hash_state[idx] == 2) {
            first_tomb = idx;
        }
        idx = (idx + 1) & (cap - 1);
    }

    if (first_tomb != (size_t)-1) {
        idx = first_tomb;
    } else {
        map->k_hash_used++;
    }
    map->k_hash_state[idx] = 1;
    map->k_hash_keys[idx] = key;
    map->k_hash_values[idx] = value;
    map->k_hash_count++;
    if (is_new) *is_new = 1;
    return 1;
}

static int ps_num_map_k_hash_delete(PSNumMap *map, uint32_t key, int *deleted) {
    if (deleted) *deleted = 0;
    if (!map || map->k_hash_cap == 0) return 0;
    size_t cap = map->k_hash_cap;
    size_t idx = (size_t)(ps_num_hash_key(key) & (cap - 1));
    size_t start = idx;
    while (map->k_hash_state[idx]) {
        if (map->k_hash_state[idx] == 1 && map->k_hash_keys[idx] == key) {
            map->k_hash_state[idx] = 2;
            map->k_hash_values[idx] = ps_value_undefined();
            map->k_hash_count--;
            if (deleted) *deleted = 1;
            return 1;
        }
        idx = (idx + 1) & (cap - 1);
        if (idx == start) break;
    }
    return 0;
}

int ps_num_map_get(PSObject *obj, size_t index, PSValue *out) {
    if (!obj || obj->kind != PS_OBJ_KIND_PLAIN) return 0;
    if (obj->internal_kind != PS_INTERNAL_NUMMAP) return 0;
    PSNumMap *map = (PSNumMap *)obj->internal;
    if (!map) return 0;
    if (index <= PS_NUM_MAP_MAX_INDEX) {
        if (index >= map->capacity) return 0;
        if (!map->present[index]) return 0;
        if (out) *out = map->items[index];
        return 1;
    }
    if (index > UINT32_MAX) return 0;
    return ps_num_map_hash_get(map, (uint32_t)index, out);
}

int ps_num_map_set(PSObject *obj, size_t index, PSValue value, int *is_new) {
    if (is_new) *is_new = 0;
    if (!obj || obj->kind != PS_OBJ_KIND_PLAIN) return 0;
    PSNumMap *map = ps_num_map_ensure(obj);
    if (!map) return 0;
    if (index <= PS_NUM_MAP_MAX_INDEX) {
        if (index >= map->capacity) {
            size_t new_cap = map->capacity ? map->capacity : 16;
            while (new_cap <= index) {
                new_cap *= 2;
            }
            if (!ps_num_map_grow(map, new_cap)) return 0;
        }
        if (!map->present[index] && is_new) {
            *is_new = 1;
        }
        map->items[index] = value;
        map->present[index] = 1;
        return 1;
    }
    if (index > UINT32_MAX) return 0;
    return ps_num_map_hash_set(map, (uint32_t)index, value, is_new);
}

int ps_num_map_delete(PSObject *obj, size_t index, int *deleted) {
    if (deleted) *deleted = 0;
    if (!obj || obj->kind != PS_OBJ_KIND_PLAIN) return 0;
    if (obj->internal_kind != PS_INTERNAL_NUMMAP) return 0;
    PSNumMap *map = (PSNumMap *)obj->internal;
    if (!map) return 0;
    if (index <= PS_NUM_MAP_MAX_INDEX) {
        if (index >= map->capacity) return 0;
        if (!map->present[index]) return 0;
        map->present[index] = 0;
        map->items[index] = ps_value_undefined();
        if (deleted) *deleted = 1;
        return 1;
    }
    if (index > UINT32_MAX) return 0;
    return ps_num_map_hash_delete(map, (uint32_t)index, deleted);
}

int ps_num_map_k_get(PSObject *obj, uint32_t key, PSValue *out) {
    if (!obj || obj->kind != PS_OBJ_KIND_PLAIN) return 0;
    if (obj->internal_kind != PS_INTERNAL_NUMMAP) return 0;
    PSNumMap *map = (PSNumMap *)obj->internal;
    if (!map) return 0;
    if (key <= PS_NUM_MAP_MAX_INDEX) {
        if (key >= map->k_capacity) return 0;
        if (!map->k_present || !map->k_present[key]) return 0;
        if (out) *out = map->k_items[key];
        return 1;
    }
    return ps_num_map_k_hash_get(map, key, out);
}

int ps_num_map_k_set(PSObject *obj, uint32_t key, PSValue value, int *is_new) {
    if (is_new) *is_new = 0;
    if (!obj || obj->kind != PS_OBJ_KIND_PLAIN) return 0;
    PSNumMap *map = ps_num_map_ensure(obj);
    if (!map) return 0;
    if (key <= PS_NUM_MAP_MAX_INDEX) {
        if (key >= map->k_capacity) {
            size_t new_cap = map->k_capacity ? map->k_capacity : 8;
            while (new_cap <= key) {
                new_cap *= 2;
            }
            if (!ps_num_map_k_grow(map, new_cap)) return 0;
        }
        if (map->k_present && !map->k_present[key] && is_new) {
            *is_new = 1;
        }
        map->k_items[key] = value;
        if (map->k_present) {
            map->k_present[key] = 1;
        }
        return 1;
    }
    return ps_num_map_k_hash_set(map, key, value, is_new);
}

int ps_num_map_k_delete(PSObject *obj, uint32_t key, int *deleted) {
    if (deleted) *deleted = 0;
    if (!obj || obj->kind != PS_OBJ_KIND_PLAIN) return 0;
    if (obj->internal_kind != PS_INTERNAL_NUMMAP) return 0;
    PSNumMap *map = (PSNumMap *)obj->internal;
    if (!map) return 0;
    if (key <= PS_NUM_MAP_MAX_INDEX) {
        if (key >= map->k_capacity) return 0;
        if (!map->k_present || !map->k_present[key]) return 0;
        map->k_present[key] = 0;
        map->k_items[key] = ps_value_undefined();
        if (deleted) *deleted = 1;
        return 1;
    }
    return ps_num_map_k_hash_delete(map, key, deleted);
}
