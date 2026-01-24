#include "ps_ast.h"
#include "ps_eval.h"
#include "ps_parser.h"
#include "ps_vm.h"
#include "ps_object.h"
#include "ps_string.h"
#include "ps_function.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <unistd.h>

static char *read_file(const char *path, size_t *out_len) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);

    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }

    size_t read = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    buf[read] = '\0';
    if (out_len) *out_len = read;
    return buf;
}

static char *read_stream(FILE *fp, size_t *out_len) {
    size_t cap = 4096;
    size_t len = 0;
    char *buf = (char *)malloc(cap + 1);
    if (!buf) return NULL;

    for (;;) {
        size_t space = cap - len;
        size_t n = fread(buf + len, 1, space, fp);
        len += n;
        if (n == 0) {
            if (ferror(fp)) {
                free(buf);
                return NULL;
            }
            break;
        }
        if (len == cap) {
            size_t new_cap = cap * 2;
            char *next = (char *)realloc(buf, new_cap + 1);
            if (!next) {
                free(buf);
                return NULL;
            }
            buf = next;
            cap = new_cap;
        }
    }

    buf[len] = '\0';
    if (out_len) *out_len = len;
    return buf;
}

static PSValue ps_native_protoscript_exit(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    int code = 0;
    if (argc > 0) {
        double num = ps_to_number(vm, argv[0]);
        if (vm && vm->has_pending_throw) return ps_value_undefined();
        if (!isnan(num) && !isinf(num)) {
            code = (int)num;
        }
    }
    exit(code);
    return ps_value_undefined();
}

static void protoscript_throw(PSVM *vm, const char *name, const char *message) {
    if (!vm) return;
    vm->pending_throw = ps_vm_make_error(vm, name ? name : "Error", message ? message : "");
    vm->has_pending_throw = 1;
}

static PSValue ps_native_protoscript_sleep(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    double num = 0.0;
    if (argc > 0) {
        num = ps_to_number(vm, argv[0]);
        if (vm && vm->has_pending_throw) return ps_value_undefined();
    }
    if (isnan(num) || isinf(num) || num < 0.0 || floor(num) != num) {
        protoscript_throw(vm, "RangeError", "Invalid sleep duration");
        return ps_value_undefined();
    }
    unsigned int seconds = (num > (double)UINT_MAX) ? UINT_MAX : (unsigned int)num;
    sleep(seconds);
    return ps_value_undefined();
}

static PSValue ps_native_protoscript_usleep(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    double num = 0.0;
    if (argc > 0) {
        num = ps_to_number(vm, argv[0]);
        if (vm && vm->has_pending_throw) return ps_value_undefined();
    }
    if (isnan(num) || isinf(num) || num < 0.0 || floor(num) != num) {
        protoscript_throw(vm, "RangeError", "Invalid sleep duration");
        return ps_value_undefined();
    }
    useconds_t usec = (num > (double)UINT_MAX) ? (useconds_t)UINT_MAX : (useconds_t)num;
    usleep(usec);
    return ps_value_undefined();
}

static PSValue ps_native_protoscript_perf_stats(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    (void)argc;
    (void)argv;

    if (!vm) return ps_value_undefined();
    PSObject *obj = ps_object_new(vm->object_proto);
    if (!obj) return ps_value_undefined();

    ps_object_define(obj,
                     ps_string_from_cstr("allocCount"),
                     ps_value_number((double)vm->perf.alloc_count),
                     PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
    ps_object_define(obj,
                     ps_string_from_cstr("allocBytes"),
                     ps_value_number((double)vm->perf.alloc_bytes),
                     PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
    ps_object_define(obj,
                     ps_string_from_cstr("objectNew"),
                     ps_value_number((double)vm->perf.object_new),
                     PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
    ps_object_define(obj,
                     ps_string_from_cstr("stringNew"),
                     ps_value_number((double)vm->perf.string_new),
                     PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
    ps_object_define(obj,
                     ps_string_from_cstr("functionNew"),
                     ps_value_number((double)vm->perf.function_new),
                     PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
    ps_object_define(obj,
                     ps_string_from_cstr("envNew"),
                     ps_value_number((double)vm->perf.env_new),
                     PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
    ps_object_define(obj,
                     ps_string_from_cstr("callCount"),
                     ps_value_number((double)vm->perf.call_count),
                     PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
    ps_object_define(obj,
                     ps_string_from_cstr("nativeCallCount"),
                     ps_value_number((double)vm->perf.native_call_count),
                     PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
    ps_object_define(obj,
                     ps_string_from_cstr("gcCollections"),
                     ps_value_number((double)vm->gc.collections),
                     PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
    ps_object_define(obj,
                     ps_string_from_cstr("gcLiveBytes"),
                     ps_value_number((double)vm->gc.live_bytes_last),
                     PS_ATTR_READONLY | PS_ATTR_DONTDELETE);

    return ps_value_object(obj);
}

static void define_protoscript_info(PSVM *vm, int argc, char **argv) {
    if (!vm || !vm->global) return;
    PSObject *info = ps_object_new(vm->object_proto);
    if (!info) return;

    PSObject *args = ps_object_new(vm->array_proto ? vm->array_proto : vm->object_proto);
    if (args) {
        args->kind = PS_OBJ_KIND_ARRAY;
        for (int i = 0; i < argc; i++) {
            char idx_buf[32];
            snprintf(idx_buf, sizeof(idx_buf), "%d", i);
            ps_object_define(args,
                             ps_string_from_cstr(idx_buf),
                             ps_value_string(ps_string_from_cstr(argv[i] ? argv[i] : "")),
                             PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
        }
        ps_object_define(args,
                         ps_string_from_cstr("length"),
                         ps_value_number((double)argc),
                         PS_ATTR_READONLY | PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
    }

    if (args) {
        ps_object_define(info,
                         ps_string_from_cstr("args"),
                         ps_value_object(args),
                         PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
    }
    ps_object_define(info,
                     ps_string_from_cstr("version"),
                     ps_value_string(ps_string_from_cstr("v1.0.0 ECMAScript 262 (ES1)")),
                     PS_ATTR_READONLY | PS_ATTR_DONTDELETE);

    PSObject *exit_fn = ps_function_new_native(ps_native_protoscript_exit);
    PSObject *sleep_fn = ps_function_new_native(ps_native_protoscript_sleep);
    PSObject *usleep_fn = ps_function_new_native(ps_native_protoscript_usleep);
    PSObject *perf_fn = ps_function_new_native(ps_native_protoscript_perf_stats);
    if (exit_fn) {
        ps_function_setup(exit_fn, vm->function_proto, vm->object_proto, NULL);
        ps_object_define(info,
                         ps_string_from_cstr("exit"),
                         ps_value_object(exit_fn),
                         PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
    }
    if (sleep_fn) {
        ps_function_setup(sleep_fn, vm->function_proto, vm->object_proto, NULL);
        ps_object_define(info,
                         ps_string_from_cstr("sleep"),
                         ps_value_object(sleep_fn),
                         PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
    }
    if (usleep_fn) {
        ps_function_setup(usleep_fn, vm->function_proto, vm->object_proto, NULL);
        ps_object_define(info,
                         ps_string_from_cstr("usleep"),
                         ps_value_object(usleep_fn),
                         PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
    }
    if (perf_fn) {
        ps_function_setup(perf_fn, vm->function_proto, vm->object_proto, NULL);
        ps_object_define(info,
                         ps_string_from_cstr("perfStats"),
                         ps_value_object(perf_fn),
                         PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
    }

    ps_object_define(vm->global,
                     ps_string_from_cstr("ProtoScript"),
                     ps_value_object(info),
                     PS_ATTR_NONE);
}

int main(int argc, char **argv) {
    size_t source_len = 0;
    char *source = NULL;
    if (argc < 2 || strcmp(argv[1], "-") == 0) {
        source = read_stream(stdin, &source_len);
    } else {
        source = read_file(argv[1], &source_len);
    }
    if (!source) {
        if (argc < 2 || strcmp(argv[1], "-") == 0) {
            fprintf(stderr, "Could not read from stdin\n");
        } else {
            fprintf(stderr, "Could not read file: %s\n", argv[1]);
        }
        return 1;
    }

    PSVM *vm = ps_vm_new();
    if (!vm) {
        fprintf(stderr, "Failed to initialize VM\n");
        free(source);
        return 1;
    }

    define_protoscript_info(vm, argc, argv);

    const char *source_path = NULL;
    if (argc >= 2 && strcmp(argv[1], "-") != 0) {
        source_path = argv[1];
    }
    PSAstNode *program = ps_parse_with_path(source, source_path);
    if (!program) {
        ps_vm_free(vm);
        free(source);
        return 1;
    }
    ps_eval(vm, program);
    ps_ast_free(program);
    ps_vm_free(vm);
    free(source);
    return 0;
}
