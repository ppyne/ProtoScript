#include "ps_env.h"
#include "ps_config.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

PSEnv *ps_env_new(PSEnv *parent, PSObject *record, int owns_record) {
    PSEnv *env = (PSEnv *)calloc(1, sizeof(PSEnv));
    if (!env) return NULL;
    env->parent = parent;
    env->record = record;
    env->owns_record = owns_record;
    env->arguments_obj = NULL;
    env->param_names = NULL;
    env->param_count = 0;
    return env;
}

PSEnv *ps_env_new_object(PSEnv *parent) {
    PSObject *record = ps_object_new(NULL);
    if (!record) return NULL;
    return ps_env_new(parent, record, 1);
}

void ps_env_free(PSEnv *env) {
    if (!env) return;
    if (env->owns_record) {
        ps_object_free(env->record);
    }
    free(env->param_names);
    free(env);
}

#if PS_ENABLE_ARGUMENTS_ALIASING
static int ps_string_equals(const PSString *a, const PSString *b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->byte_len != b->byte_len) return 0;
    return memcmp(a->utf8, b->utf8, a->byte_len) == 0;
}
#endif

PSEnv *ps_env_root(PSEnv *env) {
    PSEnv *cur = env;
    while (cur && cur->parent) {
        cur = cur->parent;
    }
    return cur;
}

int ps_env_define(PSEnv *env, PSString *name, PSValue value) {
    if (!env || !env->record) return 0;
    return ps_object_define(env->record, name, value, PS_ATTR_NONE);
}

int ps_env_set(PSEnv *env, PSString *name, PSValue value) {
    if (!env || !name) return 0;

    for (PSEnv *cur = env; cur; cur = cur->parent) {
        int found = 0;
        (void)ps_object_get_own(cur->record, name, &found);
        if (found) {
            int ok = ps_object_put(cur->record, name, value);
#if PS_ENABLE_ARGUMENTS_ALIASING
            if (ok && cur->arguments_obj && cur->param_names) {
                for (size_t i = 0; i < cur->param_count; i++) {
                    if (ps_string_equals(cur->param_names[i], name)) {
                        char buf[32];
                        snprintf(buf, sizeof(buf), "%zu", i);
                        ps_object_put(cur->arguments_obj, ps_string_from_cstr(buf), value);
                        break;
                    }
                }
            }
#endif
            return ok;
        }
    }

    PSEnv *root = ps_env_root(env);
    if (!root || !root->record) return 0;
    return ps_object_put(root->record, name, value);
}

PSValue ps_env_get(PSEnv *env, PSString *name, int *found) {
    for (PSEnv *cur = env; cur; cur = cur->parent) {
        int local_found = 0;
        PSValue v = ps_object_get_own(cur->record, name, &local_found);
        if (local_found) {
            if (found) *found = 1;
            return v;
        }
    }
    if (found) *found = 0;
    return ps_value_undefined();
}

#if PS_ENABLE_ARGUMENTS_ALIASING
static int ps_string_to_index(const PSString *s, size_t *out_index) {
    if (!s || s->byte_len == 0) return 0;
    size_t value = 0;
    for (size_t i = 0; i < s->byte_len; i++) {
        char c = s->utf8[i];
        if (c < '0' || c > '9') return 0;
        value = (value * 10) + (size_t)(c - '0');
    }
    *out_index = value;
    return 1;
}
#endif

int ps_env_update_arguments(PSEnv *env, PSObject *args_obj, PSString *prop, PSValue value) {
#if PS_ENABLE_ARGUMENTS_ALIASING
    if (!env || !args_obj || !prop) return 0;
    for (PSEnv *cur = env; cur; cur = cur->parent) {
        if (cur->arguments_obj != args_obj || !cur->param_names) continue;
        size_t index = 0;
        if (!ps_string_to_index(prop, &index)) return 0;
        if (index >= cur->param_count) return 0;
        PSString *name = cur->param_names[index];
        if (!name) return 0;
        (void)ps_object_put(cur->record, name, value);
        return 1;
    }
#endif
    (void)env;
    (void)args_obj;
    (void)prop;
    (void)value;
    return 0;
}
