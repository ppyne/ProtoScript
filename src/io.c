#include "ps_vm.h"
#include "ps_object.h"
#include "ps_value.h"
#include "ps_string.h"
#include "ps_function.h"
#include "ps_eval.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    int magic;
    FILE *fp;
    int can_read;
    int can_write;
    int is_std;
    int closed;
} PSIOFile;

static const int PS_IO_MAGIC = 0x5053494f;

static void io_throw(PSVM *vm, const char *name, const char *message) {
    if (!vm) return;
    vm->pending_throw = ps_vm_make_error(vm, name ? name : "Error", message ? message : "");
    vm->has_pending_throw = 1;
}

static PSIOFile *io_get_file(PSVM *vm, PSValue value) {
    if (value.type != PS_T_OBJECT || !value.as.object || !value.as.object->internal) {
        ps_vm_throw_type_error(vm, "Invalid file handle");
        return NULL;
    }
    PSIOFile *file = (PSIOFile *)value.as.object->internal;
    if (file->magic != PS_IO_MAGIC) {
        ps_vm_throw_type_error(vm, "Invalid file handle");
        return NULL;
    }
    if (file->closed) {
        io_throw(vm, "Error", "File is closed");
        return NULL;
    }
    return file;
}

static PSObject *io_make_file(PSVM *vm, FILE *fp, int can_read, int can_write, int is_std) {
    PSObject *obj = ps_object_new(vm ? vm->object_proto : NULL);
    if (!obj) return NULL;
    PSIOFile *file = (PSIOFile *)calloc(1, sizeof(PSIOFile));
    if (!file) return NULL;
    file->magic = PS_IO_MAGIC;
    file->fp = fp;
    file->can_read = can_read;
    file->can_write = can_write;
    file->is_std = is_std;
    file->closed = 0;
    obj->internal = file;
    return obj;
}

static char *io_string_cstr(PSVM *vm, PSString *s) {
    if (!s) return NULL;
    if (memchr(s->utf8, '\0', s->byte_len)) {
        io_throw(vm, "Error", "Invalid string data");
        return NULL;
    }
    char *out = (char *)malloc(s->byte_len + 1);
    if (!out) return NULL;
    memcpy(out, s->utf8, s->byte_len);
    out[s->byte_len] = '\0';
    return out;
}

static PSValue io_return_string(PSVM *vm, const char *data, size_t len) {
    PSString *s = NULL;
    if (len == 0) {
        s = ps_string_from_cstr("");
    } else {
        s = ps_string_from_utf8(data, len);
    }
    if (!s) {
        io_throw(vm, "Error", "Invalid UTF-8 data");
        return ps_value_undefined();
    }
    return ps_value_string(s);
}

/* --------------------------------------------------------- */
/* Internal builtin: Io.print                                */
/* --------------------------------------------------------- */

/* Native implementation of Io.print */
static PSValue ps_native_print(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    if (argc > 0) {
        PSString *s = ps_to_string(vm, argv[0]);
        if (s) {
            fwrite(s->utf8, 1, s->byte_len, stdout);
        }
    }
    return ps_value_undefined();
}

static PSValue ps_native_open(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (argc < 2) {
        ps_vm_throw_type_error(vm, "Io.open expects (path, mode)");
        return ps_value_undefined();
    }
    PSString *path_s = ps_to_string(vm, argv[0]);
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    PSString *mode_s = ps_to_string(vm, argv[1]);
    if (vm && vm->has_pending_throw) return ps_value_undefined();

    char *path = io_string_cstr(vm, path_s);
    if (vm && vm->has_pending_throw) {
        free(path);
        return ps_value_undefined();
    }
    char *mode = io_string_cstr(vm, mode_s);
    if (vm && vm->has_pending_throw) {
        free(path);
        free(mode);
        return ps_value_undefined();
    }

    int can_read = 0;
    int can_write = 0;
    const char *fmode = NULL;
    if (strcmp(mode, "r") == 0) {
        fmode = "rb";
        can_read = 1;
    } else if (strcmp(mode, "w") == 0) {
        fmode = "wb";
        can_write = 1;
    } else if (strcmp(mode, "a") == 0) {
        fmode = "ab";
        can_write = 1;
    } else {
        io_throw(vm, "Error", "Invalid file mode");
        free(path);
        free(mode);
        return ps_value_undefined();
    }

    FILE *fp = fopen(path, fmode);
    free(path);
    free(mode);
    if (!fp) {
        io_throw(vm, "Error", "Unable to open file");
        return ps_value_undefined();
    }

    PSObject *obj = io_make_file(vm, fp, can_read, can_write, 0);
    if (!obj) {
        fclose(fp);
        return ps_value_undefined();
    }
    return ps_value_object(obj);
}

static PSValue ps_native_read(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (argc < 1) {
        ps_vm_throw_type_error(vm, "Io.read expects (file)");
        return ps_value_undefined();
    }
    PSIOFile *file = io_get_file(vm, argv[0]);
    if (!file || (vm && vm->has_pending_throw)) return ps_value_undefined();
    if (!file->can_read) {
        io_throw(vm, "Error", "File not open for reading");
        return ps_value_undefined();
    }

    char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;
    char temp[4096];
    for (;;) {
        size_t n = fread(temp, 1, sizeof(temp), file->fp);
        if (n > 0) {
            if (len + n > cap) {
                size_t next = cap ? cap * 2 : 4096;
                while (next < len + n) next *= 2;
                char *tmp = (char *)realloc(buf, next);
                if (!tmp) {
                    free(buf);
                    return ps_value_undefined();
                }
                buf = tmp;
                cap = next;
            }
            memcpy(buf + len, temp, n);
            len += n;
        }
        if (n < sizeof(temp)) {
            if (ferror(file->fp)) {
                free(buf);
                io_throw(vm, "Error", "Read error");
                return ps_value_undefined();
            }
            break;
        }
    }
    PSValue out = io_return_string(vm, buf ? buf : "", len);
    free(buf);
    return out;
}

static PSValue ps_native_read_lines(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (argc < 1) {
        ps_vm_throw_type_error(vm, "Io.readLines expects (file)");
        return ps_value_undefined();
    }
    PSIOFile *file = io_get_file(vm, argv[0]);
    if (!file || (vm && vm->has_pending_throw)) return ps_value_undefined();
    if (!file->can_read) {
        io_throw(vm, "Error", "File not open for reading");
        return ps_value_undefined();
    }

    PSObject *arr = ps_object_new(vm->array_proto ? vm->array_proto : vm->object_proto);
    if (!arr) return ps_value_undefined();
    arr->kind = PS_OBJ_KIND_ARRAY;

    char *line = NULL;
    size_t len = 0;
    size_t cap = 0;
    size_t index = 0;

    for (;;) {
        int ch = fgetc(file->fp);
        if (ch == EOF) {
            if (ferror(file->fp)) {
                free(line);
                io_throw(vm, "Error", "Read error");
                return ps_value_undefined();
            }
            if (len > 0) {
                PSValue s = io_return_string(vm, line ? line : "", len);
                if (vm && vm->has_pending_throw) {
                    free(line);
                    return ps_value_undefined();
                }
                char buf[32];
                snprintf(buf, sizeof(buf), "%zu", index++);
                ps_object_define(arr, ps_string_from_cstr(buf), s, PS_ATTR_NONE);
            }
            break;
        }
        if (ch == 0) {
            free(line);
            io_throw(vm, "Error", "NUL character in input");
            return ps_value_undefined();
        }
        if (ch == '\n') {
            PSValue s = io_return_string(vm, line ? line : "", len);
            if (vm && vm->has_pending_throw) {
                free(line);
                return ps_value_undefined();
            }
            char buf[32];
            snprintf(buf, sizeof(buf), "%zu", index++);
            ps_object_define(arr, ps_string_from_cstr(buf), s, PS_ATTR_NONE);
            len = 0;
            continue;
        }
        if (len + 1 > cap) {
            size_t next = cap ? cap * 2 : 64;
            char *tmp = (char *)realloc(line, next);
            if (!tmp) {
                free(line);
                return ps_value_undefined();
            }
            line = tmp;
            cap = next;
        }
        line[len++] = (char)ch;
    }
    free(line);
    ps_object_define(arr,
                     ps_string_from_cstr("length"),
                     ps_value_number((double)index),
                     PS_ATTR_NONE);
    return ps_value_object(arr);
}

static PSValue ps_native_write(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (argc < 2) {
        ps_vm_throw_type_error(vm, "Io.write expects (file, data)");
        return ps_value_undefined();
    }
    PSIOFile *file = io_get_file(vm, argv[0]);
    if (!file || (vm && vm->has_pending_throw)) return ps_value_undefined();
    if (!file->can_write) {
        io_throw(vm, "Error", "File not open for writing");
        return ps_value_undefined();
    }
    PSString *s = ps_to_string(vm, argv[1]);
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    if (s && s->byte_len > 0) {
        size_t n = fwrite(s->utf8, 1, s->byte_len, file->fp);
        if (n != s->byte_len) {
            io_throw(vm, "Error", "Write error");
            return ps_value_undefined();
        }
    }
    return ps_value_undefined();
}

static PSValue ps_native_write_line(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (argc < 2) {
        ps_vm_throw_type_error(vm, "Io.writeLine expects (file, data)");
        return ps_value_undefined();
    }
    PSValue args[2];
    args[0] = argv[0];
    args[1] = argv[1];
    PSValue res = ps_native_write(vm, this_val, 2, args);
    if (vm && vm->has_pending_throw) return res;
    PSValue eol = ps_value_string(ps_string_from_cstr("\n"));
    args[1] = eol;
    return ps_native_write(vm, this_val, 2, args);
}

static PSValue ps_native_close(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (argc < 1) {
        ps_vm_throw_type_error(vm, "Io.close expects (file)");
        return ps_value_undefined();
    }
    PSIOFile *file = io_get_file(vm, argv[0]);
    if (!file || (vm && vm->has_pending_throw)) return ps_value_undefined();
    if (file->is_std) {
        io_throw(vm, "Error", "Cannot close standard stream");
        return ps_value_undefined();
    }
    if (file->fp) {
        fclose(file->fp);
    }
    file->closed = 1;
    return ps_value_undefined();
}

static PSValue ps_native_temp_path(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    const char *dir = getenv("TMPDIR");
    if (!dir || dir[0] == '\0') dir = "/tmp";
    char buf[512];
    snprintf(buf, sizeof(buf), "%s/protoscriptXXXXXX", dir);
    int fd = mkstemp(buf);
    if (fd < 0) {
        io_throw(vm, "Error", "Unable to create temp path");
        return ps_value_undefined();
    }
    close(fd);
    unlink(buf);
    return ps_value_string(ps_string_from_cstr(buf));
}

/* --------------------------------------------------------- */
/* Io object initialization                                  */
/* --------------------------------------------------------- */

void ps_io_init(PSVM *vm) {
    if (!vm || !vm->global) return;

    /* Create Io object */
    PSObject *io = ps_object_new(NULL);
    if (!io) return;

    /* Create a function object for print */
    PSObject *print_fn = ps_function_new_native(ps_native_print);
    if (!print_fn) return;
    ps_function_setup(print_fn, vm->function_proto, vm->object_proto, NULL);

    PSObject *open_fn = ps_function_new_native(ps_native_open);
    PSObject *read_fn = ps_function_new_native(ps_native_read);
    PSObject *read_lines_fn = ps_function_new_native(ps_native_read_lines);
    PSObject *write_fn = ps_function_new_native(ps_native_write);
    PSObject *write_line_fn = ps_function_new_native(ps_native_write_line);
    PSObject *close_fn = ps_function_new_native(ps_native_close);
    PSObject *temp_path_fn = ps_function_new_native(ps_native_temp_path);
    if (open_fn) ps_function_setup(open_fn, vm->function_proto, vm->object_proto, NULL);
    if (read_fn) ps_function_setup(read_fn, vm->function_proto, vm->object_proto, NULL);
    if (read_lines_fn) ps_function_setup(read_lines_fn, vm->function_proto, vm->object_proto, NULL);
    if (write_fn) ps_function_setup(write_fn, vm->function_proto, vm->object_proto, NULL);
    if (write_line_fn) ps_function_setup(write_line_fn, vm->function_proto, vm->object_proto, NULL);
    if (close_fn) ps_function_setup(close_fn, vm->function_proto, vm->object_proto, NULL);
    if (temp_path_fn) ps_function_setup(temp_path_fn, vm->function_proto, vm->object_proto, NULL);

    /* Attach print to Io */
    ps_object_define(
        io,
        ps_string_from_cstr("print"),
        ps_value_object(print_fn),
        PS_ATTR_NONE
    );

    if (open_fn) {
        ps_object_define(io, ps_string_from_cstr("open"), ps_value_object(open_fn), PS_ATTR_NONE);
    }
    if (read_fn) {
        ps_object_define(io, ps_string_from_cstr("read"), ps_value_object(read_fn), PS_ATTR_NONE);
    }
    if (read_lines_fn) {
        ps_object_define(io, ps_string_from_cstr("readLines"), ps_value_object(read_lines_fn), PS_ATTR_NONE);
    }
    if (write_fn) {
        ps_object_define(io, ps_string_from_cstr("write"), ps_value_object(write_fn), PS_ATTR_NONE);
    }
    if (write_line_fn) {
        ps_object_define(io, ps_string_from_cstr("writeLine"), ps_value_object(write_line_fn), PS_ATTR_NONE);
    }
    if (close_fn) {
        ps_object_define(io, ps_string_from_cstr("close"), ps_value_object(close_fn), PS_ATTR_NONE);
    }
    if (temp_path_fn) {
        ps_object_define(io, ps_string_from_cstr("tempPath"), ps_value_object(temp_path_fn), PS_ATTR_NONE);
    }

    ps_object_define(io,
                     ps_string_from_cstr("EOL"),
                     ps_value_string(ps_string_from_cstr("\n")),
                     PS_ATTR_READONLY | PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);

    PSObject *stdin_obj = io_make_file(vm, stdin, 1, 0, 1);
    PSObject *stdout_obj = io_make_file(vm, stdout, 0, 1, 1);
    PSObject *stderr_obj = io_make_file(vm, stderr, 0, 1, 1);
    if (stdin_obj) {
        ps_object_define(io, ps_string_from_cstr("stdin"), ps_value_object(stdin_obj), PS_ATTR_NONE);
    }
    if (stdout_obj) {
        ps_object_define(io, ps_string_from_cstr("stdout"), ps_value_object(stdout_obj), PS_ATTR_NONE);
    }
    if (stderr_obj) {
        ps_object_define(io, ps_string_from_cstr("stderr"), ps_value_object(stderr_obj), PS_ATTR_NONE);
    }

    /* Attach Io to Global Object */
    ps_object_define(
        vm->global,
        ps_string_from_cstr("Io"),
        ps_value_object(io),
        PS_ATTR_NONE
    );
}
