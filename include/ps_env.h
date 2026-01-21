#ifndef PS_ENV_H
#define PS_ENV_H

#include "ps_object.h"
#include "ps_value.h"

typedef struct PSEnv {
    struct PSEnv *parent;
    PSObject     *record;
    int           owns_record;
    PSObject     *arguments_obj;
    PSString    **param_names;
    size_t        param_count;
} PSEnv;

PSEnv  *ps_env_new(PSEnv *parent, PSObject *record, int owns_record);
PSEnv  *ps_env_new_object(PSEnv *parent);
void    ps_env_free(PSEnv *env);

int     ps_env_define(PSEnv *env, PSString *name, PSValue value);
int     ps_env_set(PSEnv *env, PSString *name, PSValue value);
PSValue ps_env_get(PSEnv *env, PSString *name, int *found);
PSEnv  *ps_env_root(PSEnv *env);
int     ps_env_update_arguments(PSEnv *env, PSObject *args_obj, PSString *prop, PSValue value);

#endif /* PS_ENV_H */
