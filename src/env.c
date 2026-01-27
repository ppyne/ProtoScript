#include "ps_env.h"
#include "ps_config.h"
#include "ps_gc.h"
#include "ps_vm.h"
#include "ps_function.h"
#include "ps_ast.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

PSEnv *ps_env_new(PSEnv *parent, PSObject *record, int owns_record) {
    PSEnv *env = (PSEnv *)ps_gc_alloc(PS_GC_ENV, sizeof(PSEnv));
    if (!env) return NULL;
#if PS_ENABLE_PERF
    {
        PSVM *vm = ps_gc_active_vm();
        if (vm) {
            vm->perf.env_new++;
        }
    }
#endif
    env->parent = parent;
    env->record = record;
    env->owns_record = owns_record;
    env->arguments_obj = NULL;
    env->callee_obj = NULL;
    env->arguments_values = NULL;
    env->arguments_count = 0;
    env->fast_names = NULL;
    env->fast_values = NULL;
    env->fast_count = 0;
    env->param_names = NULL;
    env->param_count = 0;
    env->param_names_owned = 0;
    env->is_with = 0;
    return env;
}

PSEnv *ps_env_new_object(PSEnv *parent) {
    PSObject *record = ps_object_new(NULL);
    if (!record) return NULL;
    return ps_env_new(parent, record, 1);
}

void ps_env_free(PSEnv *env) {
    if (!env) return;
    if (ps_gc_is_managed(env)) {
        return;
    }
    if (env->owns_record) {
        ps_object_free(env->record);
    }
    if (env->param_names_owned) {
        free(env->param_names);
    }
    free(env->fast_values);
    free(env);
}

static int ps_string_equals_cstr(const PSString *s, const char *lit) {
    if (!s || !lit) return 0;
    size_t len = strlen(lit);
    if (s->byte_len != len) return 0;
    return memcmp(s->utf8, lit, len) == 0;
}

static int ps_string_equals(const PSString *a, const PSString *b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->byte_len != b->byte_len) return 0;
    return memcmp(a->utf8, b->utf8, a->byte_len) == 0;
}

PSEnv *ps_env_root(PSEnv *env) {
    PSEnv *cur = env;
    while (cur && cur->parent) {
        cur = cur->parent;
    }
    return cur;
}

int ps_env_define(PSEnv *env, PSString *name, PSValue value) {
    if (!env || !name) return 0;
    int fast_found = 0;
    if (env->fast_names && env->fast_values) {
        for (size_t i = 0; i < env->fast_count; i++) {
            if (env->fast_names[i] && ps_string_equals(env->fast_names[i], name)) {
                env->fast_values[i] = value;
                fast_found = 1;
                break;
            }
        }
    }
    if (!env->record) {
        return fast_found;
    }
    return ps_object_define(env->record, name, value, PS_ATTR_NONE);
}

int ps_env_set(PSEnv *env, PSString *name, PSValue value) {
    if (!env || !name) return 0;

    for (PSEnv *cur = env; cur; cur = cur->parent) {
        if (cur->fast_names && cur->fast_values) {
            for (size_t i = 0; i < cur->fast_count; i++) {
                if (cur->fast_names[i] && ps_string_equals(cur->fast_names[i], name)) {
                    cur->fast_values[i] = value;
                    if (cur->record) {
                        (void)ps_object_put(cur->record, name, value);
                    }
#if PS_ENABLE_ARGUMENTS_ALIASING
                    if (cur->arguments_obj && cur->param_names) {
                        for (size_t j = 0; j < cur->param_count; j++) {
                            if (ps_string_equals(cur->param_names[j], name)) {
                                char buf[32];
                                snprintf(buf, sizeof(buf), "%zu", j);
                                ps_object_put(cur->arguments_obj, ps_string_from_cstr(buf), value);
                                break;
                            }
                        }
                    }
#endif
                    return 1;
                }
            }
        }
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
        if (cur->fast_names && cur->fast_values) {
            for (size_t i = 0; i < cur->fast_count; i++) {
                if (cur->fast_names[i] && ps_string_equals(cur->fast_names[i], name)) {
                    if (found) *found = 1;
                    return cur->fast_values[i];
                }
            }
        }
        int local_found = 0;
        PSValue v = ps_object_get_own(cur->record, name, &local_found);
        if (local_found) {
            if (found) *found = 1;
            return v;
        }
        if (ps_string_equals_cstr(name, "arguments") &&
            cur->callee_obj) {
            if (!cur->arguments_obj) {
                PSVM *vm = ps_gc_active_vm();
                PSObject *args_obj = ps_object_new(vm ? vm->object_proto : NULL);
                if (args_obj) {
                    PSFunction *func = ps_function_from_object(cur->callee_obj);
                    for (size_t i = 0; i < cur->arguments_count; i++) {
                        PSValue val = cur->arguments_values ? cur->arguments_values[i] : ps_value_undefined();
                        if (func && i < func->param_count && func->param_names) {
                            PSString *param_name = func->param_names[i];
                            if (param_name) {
                                int found_param = 0;
                                PSValue current = ps_object_get_own(cur->record, param_name, &found_param);
                                if (found_param) val = current;
                            }
                        }
                        char buf[32];
                        snprintf(buf, sizeof(buf), "%zu", i);
                        ps_object_define(args_obj, ps_string_from_cstr(buf), val, PS_ATTR_NONE);
                    }
                    ps_object_define(args_obj,
                                     ps_string_from_cstr("length"),
                                     ps_value_number((double)cur->arguments_count),
                                     PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
                    ps_object_define(args_obj,
                                     ps_string_from_cstr("callee"),
                                     ps_value_object(cur->callee_obj),
                                     PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE | PS_ATTR_READONLY);
                    ps_object_define(cur->record,
                                     ps_string_from_cstr("arguments"),
                                     ps_value_object(args_obj),
                                     PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
                    cur->arguments_obj = args_obj;
                }
            }
            if (cur->arguments_obj) {
                if (found) *found = 1;
                return ps_value_object(cur->arguments_obj);
            }
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
        if (cur->fast_values) {
            if (cur->fast_names) {
                for (size_t i = 0; i < cur->fast_count; i++) {
                    if (cur->fast_names[i] && ps_string_equals(cur->fast_names[i], name)) {
                        cur->fast_values[i] = value;
                        break;
                    }
                }
            } else if (index < cur->fast_count) {
                cur->fast_values[index] = value;
            }
        }
        return 1;
    }
#endif
    (void)env;
    (void)args_obj;
    (void)prop;
    (void)value;
    return 0;
}
