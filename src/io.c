#include "ps_vm.h"
#include "ps_object.h"
#include "ps_value.h"
#include "ps_string.h"
#include "ps_function.h"
#include "ps_eval.h"
#include "ps_buffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

typedef struct {
    int magic;
    FILE *fp;
    int can_read;
    int can_write;
    int is_std;
    int closed;
    int binary;
    unsigned char bom_tail[2];
    size_t bom_tail_len;
} PSIOFile;

static const int PS_IO_MAGIC = 0x5053494f;
static PSObject *g_io_eof_obj = NULL;
static PSObject *g_io_file_proto = NULL;

static void io_throw(PSVM *vm, const char *name, const char *message) {
    if (!vm) return;
    vm->pending_throw = ps_vm_make_error(vm, name ? name : "Error", message ? message : "");
    vm->has_pending_throw = 1;
}

static PSIOFile *io_get_file(PSVM *vm, PSValue value, int allow_closed) {
    if (value.type != PS_T_OBJECT || !value.as.object || !value.as.object->internal) {
        ps_vm_throw_type_error(vm, "Invalid file handle");
        return NULL;
    }
    PSIOFile *file = (PSIOFile *)value.as.object->internal;
    if (file->magic != PS_IO_MAGIC) {
        ps_vm_throw_type_error(vm, "Invalid file handle");
        return NULL;
    }
    if (!allow_closed && file->closed) {
        io_throw(vm, "Error", "File is closed");
        return NULL;
    }
    return file;
}

static PSObject *io_make_file(PSVM *vm,
                              FILE *fp,
                              int can_read,
                              int can_write,
                              int is_std,
                              int binary,
                              PSString *path,
                              PSString *mode) {
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
    file->binary = binary;
    file->bom_tail_len = 0;
    obj->internal = file;
    if (g_io_file_proto) {
        obj->prototype = g_io_file_proto;
    }
    if (path) {
        ps_object_define(obj, ps_string_from_cstr("path"), ps_value_string(path), PS_ATTR_NONE);
    }
    if (mode) {
        ps_object_define(obj, ps_string_from_cstr("mode"), ps_value_string(mode), PS_ATTR_NONE);
    }
    ps_object_define(obj, ps_string_from_cstr("closed"), ps_value_boolean(0), PS_ATTR_NONE);
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

static int io_parse_mode(const char *mode, int *can_read, int *can_write, int *binary) {
    int r = 0;
    int w = 0;
    int a = 0;
    int b = 0;
    if (!mode || !mode[0]) return 0;
    for (const char *p = mode; *p; p++) {
        if (*p == 'r') r = 1;
        else if (*p == 'w') w = 1;
        else if (*p == 'a') a = 1;
        else if (*p == 'b') b = 1;
        else return 0;
    }
    if ((r + w + a) != 1) return 0;
    if (can_read) *can_read = r;
    if (can_write) *can_write = (w || a);
    if (binary) *binary = b;
    return 1;
}

static void io_set_closed_property(PSObject *obj, int closed) {
    if (!obj) return;
    ps_object_put(obj, ps_string_from_cstr("closed"), ps_value_boolean(closed));
}

static void io_consume_bom(PSIOFile *file) {
    if (!file || file->binary || !file->fp) return;
    int b1 = fgetc(file->fp);
    if (b1 == EOF) return;
    int b2 = fgetc(file->fp);
    if (b2 == EOF) {
        ungetc(b1, file->fp);
        return;
    }
    int b3 = fgetc(file->fp);
    if (b3 == EOF) {
        ungetc(b2, file->fp);
        ungetc(b1, file->fp);
        return;
    }
    if ((unsigned char)b1 == 0xEF && (unsigned char)b2 == 0xBB && (unsigned char)b3 == 0xBF) {
        return;
    }
    ungetc(b3, file->fp);
    ungetc(b2, file->fp);
    ungetc(b1, file->fp);
}

static int io_contains_bom_sequence(const unsigned char *buf, size_t len) {
    if (!buf || len == 0) return 0;
    for (size_t i = 0; i + 2 < len; i++) {
        if (buf[i] == 0xEF && buf[i + 1] == 0xBB && buf[i + 2] == 0xBF) {
            return 1;
        }
    }
    for (size_t i = 0; i + 1 < len; i++) {
        if ((buf[i] == 0xFF && buf[i + 1] == 0xFE) ||
            (buf[i] == 0xFE && buf[i + 1] == 0xFF)) {
            return 1;
        }
    }
    return 0;
}

static int io_check_text_data(PSVM *vm, PSIOFile *file, const unsigned char *buf, size_t len) {
    if (!file) return 0;
    if (len > 0 && memchr(buf, '\0', len)) {
        io_throw(vm, "Error", "NUL character in input");
        return 0;
    }
    if (file->bom_tail_len > 0) {
        unsigned char tmp[5];
        size_t prefix = len < 2 ? len : 2;
        memcpy(tmp, file->bom_tail, file->bom_tail_len);
        memcpy(tmp + file->bom_tail_len, buf, prefix);
        if (io_contains_bom_sequence(tmp, file->bom_tail_len + prefix)) {
            io_throw(vm, "Error", "Invalid BOM in input");
            return 0;
        }
    }
    if (io_contains_bom_sequence(buf, len)) {
        io_throw(vm, "Error", "Invalid BOM in input");
        return 0;
    }
    if (len >= 2) {
        file->bom_tail_len = 2;
        file->bom_tail[0] = buf[len - 2];
        file->bom_tail[1] = buf[len - 1];
    } else if (len == 1) {
        file->bom_tail_len = 1;
        file->bom_tail[0] = buf[0];
    }
    return 1;
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

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} PSSprintfBuf;

static int sprintf_buf_reserve(PSSprintfBuf *buf, size_t needed) {
    if (!buf) return 0;
    if (needed <= buf->cap) return 1;
    size_t next = buf->cap ? buf->cap : 64;
    while (next < needed) next *= 2;
    char *data = (char *)realloc(buf->data, next);
    if (!data) return 0;
    buf->data = data;
    buf->cap = next;
    return 1;
}

static int sprintf_buf_append(PSSprintfBuf *buf, const char *data, size_t len) {
    if (!buf || !data || len == 0) return 1;
    if (!sprintf_buf_reserve(buf, buf->len + len + 1)) return 0;
    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
    buf->data[buf->len] = '\0';
    return 1;
}

static int sprintf_buf_append_char(PSSprintfBuf *buf, char c, size_t count) {
    if (!buf || count == 0) return 1;
    if (!sprintf_buf_reserve(buf, buf->len + count + 1)) return 0;
    memset(buf->data + buf->len, c, count);
    buf->len += count;
    buf->data[buf->len] = '\0';
    return 1;
}

static int sprintf_append_padded(PSSprintfBuf *buf,
                                 const char *data,
                                 size_t len,
                                 int width,
                                 int left_align,
                                 char pad) {
    size_t pad_len = 0;
    if (width > 0 && (size_t)width > len) pad_len = (size_t)width - len;
    if (!left_align && pad_len > 0) {
        if (!sprintf_buf_append_char(buf, pad, pad_len)) return 0;
    }
    if (!sprintf_buf_append(buf, data, len)) return 0;
    if (left_align && pad_len > 0) {
        if (!sprintf_buf_append_char(buf, ' ', pad_len)) return 0;
    }
    return 1;
}

static int sprintf_append_signed(PSSprintfBuf *buf,
                                 const char *data,
                                 size_t len,
                                 int width,
                                 int left_align,
                                 char pad) {
    if (pad != '0' || left_align || len == 0 || (data[0] != '-' && data[0] != '+')) {
        return sprintf_append_padded(buf, data, len, width, left_align, pad);
    }
    size_t pad_len = 0;
    if (width > 0 && (size_t)width > len) pad_len = (size_t)width - len;
    if (!sprintf_buf_append(buf, data, 1)) return 0;
    if (pad_len > 0 && !sprintf_buf_append_char(buf, '0', pad_len)) return 0;
    return sprintf_buf_append(buf, data + 1, len - 1);
}

static void sprintf_parse_flags(const char *fmt,
                                size_t len,
                                size_t *idx,
                                int *left_align,
                                int *zero_pad) {
    while (*idx < len) {
        char c = fmt[*idx];
        if (c == '-') {
            *left_align = 1;
            (*idx)++;
        } else if (c == '0') {
            *zero_pad = 1;
            (*idx)++;
        } else {
            break;
        }
    }
}

static int sprintf_parse_number(const char *fmt, size_t len, size_t *idx, int *out) {
    int value = 0;
    int found = 0;
    while (*idx < len) {
        char c = fmt[*idx];
        if (c < '0' || c > '9') break;
        found = 1;
        value = value * 10 + (c - '0');
        (*idx)++;
    }
    if (out) *out = found ? value : -1;
    return found;
}

static int sprintf_format_number(char *out,
                                 size_t out_len,
                                 char spec,
                                 double num,
                                 int precision) {
    if (isnan(num)) {
        snprintf(out, out_len, "NaN");
        return 1;
    }
    if (isinf(num)) {
        snprintf(out, out_len, "%sInfinity", num < 0 ? "-" : "");
        return 1;
    }
    if (spec == 'f') {
        if (precision >= 0) {
            snprintf(out, out_len, "%.*f", precision, num);
        } else {
            snprintf(out, out_len, "%f", num);
        }
        return 1;
    }
    long long ival = (long long)num;
    if (spec == 'd' || spec == 'i') {
        snprintf(out, out_len, "%lld", ival);
        return 1;
    }
    if (spec == 'x') {
        snprintf(out, out_len, "%llx", (unsigned long long)ival);
        return 1;
    }
    if (spec == 'X') {
        snprintf(out, out_len, "%llX", (unsigned long long)ival);
        return 1;
    }
    if (spec == 'o') {
        snprintf(out, out_len, "%llo", (unsigned long long)ival);
        return 1;
    }
    return 0;
}

static PSValue ps_native_sprintf(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (argc < 1) {
        ps_vm_throw_type_error(vm, "Io.sprintf expects (format, ...args)");
        return ps_value_undefined();
    }
    if (argv[0].type != PS_T_STRING) {
        ps_vm_throw_type_error(vm, "Io.sprintf expects (format, ...args)");
        return ps_value_undefined();
    }
    PSString *fmt_s = argv[0].as.string;
    if (!fmt_s) return ps_value_string(ps_string_from_cstr(""));

    PSSprintfBuf out = {0};
    size_t i = 0;
    int arg_index = 1;
    while (i < fmt_s->byte_len) {
        char c = fmt_s->utf8[i];
        if (c != '%') {
            if (!sprintf_buf_append(&out, &c, 1)) break;
            i++;
            continue;
        }
        i++;
        if (i >= fmt_s->byte_len) {
            (void)sprintf_buf_append(&out, "%", 1);
            break;
        }
        if (fmt_s->utf8[i] == '%') {
            (void)sprintf_buf_append(&out, "%", 1);
            i++;
            continue;
        }

        int left_align = 0;
        int zero_pad = 0;
        int width = -1;
        int precision = -1;
        sprintf_parse_flags(fmt_s->utf8, fmt_s->byte_len, &i, &left_align, &zero_pad);
        sprintf_parse_number(fmt_s->utf8, fmt_s->byte_len, &i, &width);
        if (i < fmt_s->byte_len && fmt_s->utf8[i] == '.') {
            i++;
            sprintf_parse_number(fmt_s->utf8, fmt_s->byte_len, &i, &precision);
            if (precision < 0) precision = 0;
        }
        if (i >= fmt_s->byte_len) {
            (void)sprintf_buf_append(&out, "%", 1);
            break;
        }

        char spec = fmt_s->utf8[i++];
        PSValue arg = ps_value_undefined();
        if (arg_index < argc) {
            arg = argv[arg_index];
        }
        if (spec != '%') arg_index++;

        char pad = (zero_pad && !left_align) ? '0' : ' ';
        if (spec == 's') {
            PSString *s = ps_to_string(vm, arg);
            if (s) {
                (void)sprintf_append_padded(&out, s->utf8, s->byte_len, width, left_align, pad);
            }
            continue;
        }
        if (spec == 'd' || spec == 'i' || spec == 'f' || spec == 'x' || spec == 'X' || spec == 'o') {
            double num = ps_to_number(vm, arg);
            char tmp[128];
            if (!sprintf_format_number(tmp, sizeof(tmp), spec, num, precision)) {
                (void)sprintf_buf_append(&out, "%", 1);
                (void)sprintf_buf_append(&out, &spec, 1);
                continue;
            }
            size_t len = strlen(tmp);
            if (spec == 'd' || spec == 'i' || spec == 'f') {
                (void)sprintf_append_signed(&out, tmp, len, width, left_align, pad);
            } else {
                (void)sprintf_append_padded(&out, tmp, len, width, left_align, pad);
            }
            continue;
        }

        (void)sprintf_buf_append(&out, "%", 1);
        (void)sprintf_buf_append(&out, &spec, 1);
    }

    PSValue result = ps_value_string(ps_string_from_utf8(out.data ? out.data : "", out.len));
    free(out.data);
    return result;
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
    int binary = 0;
    if (!io_parse_mode(mode, &can_read, &can_write, &binary)) {
        io_throw(vm, "Error", "Invalid file mode");
        free(path);
        free(mode);
        return ps_value_undefined();
    }
    const char *fmode = can_read ? "rb" : (mode[0] == 'a' || strchr(mode, 'a')) ? "ab" : "wb";

    FILE *fp = fopen(path, fmode);
    free(path);
    free(mode);
    if (!fp) {
        io_throw(vm, "Error", "Unable to open file");
        return ps_value_undefined();
    }

    PSObject *obj = io_make_file(vm, fp, can_read, can_write, 0, binary, path_s, mode_s);
    if (!obj) {
        fclose(fp);
        return ps_value_undefined();
    }
    if (can_read && !binary) {
        io_consume_bom((PSIOFile *)obj->internal);
    }
    return ps_value_object(obj);
}

static int io_parse_size_arg(PSVM *vm, PSValue value, size_t *out_size) {
    double num = ps_to_number(vm, value);
    if (vm && vm->has_pending_throw) return 0;
    if (isnan(num) || isinf(num) || num < 0.0 || floor(num) != num) {
        ps_vm_throw_type_error(vm, "Invalid size");
        return 0;
    }
    if (num > (double)SIZE_MAX) {
        ps_vm_throw_type_error(vm, "Invalid size");
        return 0;
    }
    if (out_size) *out_size = (size_t)num;
    return 1;
}

static PSValue io_make_buffer(PSVM *vm, const unsigned char *data, size_t len) {
    PSObject *buf_obj = ps_buffer_new(vm, len);
    if (!buf_obj) return ps_value_undefined();
    PSBuffer *buf = ps_buffer_from_object(buf_obj);
    if (len > 0 && buf && buf->data) {
        memcpy(buf->data, data, len);
    }
    return ps_value_object(buf_obj);
}

static PSValue ps_native_file_read(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    if (this_val.type != PS_T_OBJECT || !this_val.as.object) {
        ps_vm_throw_type_error(vm, "Invalid file handle");
        return ps_value_undefined();
    }
    PSIOFile *file = io_get_file(vm, this_val, 0);
    if (!file || (vm && vm->has_pending_throw)) return ps_value_undefined();
    if (!file->can_read) {
        io_throw(vm, "Error", "File not open for reading");
        return ps_value_undefined();
    }

    if (argc == 0 || argv[0].type == PS_T_UNDEFINED) {
        unsigned char *buf = NULL;
        size_t len = 0;
        size_t cap = 0;
        unsigned char temp[4096];
        for (;;) {
            size_t n = fread(temp, 1, sizeof(temp), file->fp);
            if (n > 0) {
                if (len + n > cap) {
                    size_t next = cap ? cap * 2 : 4096;
                    while (next < len + n) next *= 2;
                    unsigned char *tmp = (unsigned char *)realloc(buf, next);
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
        if (file->binary) {
            PSValue out = io_make_buffer(vm, buf, len);
            free(buf);
            return out;
        }
        if (!io_check_text_data(vm, file, buf, len)) {
            free(buf);
            return ps_value_undefined();
        }
        PSValue out = io_return_string(vm, (const char *)(buf ? buf : (unsigned char *)""), len);
        free(buf);
        return out;
    }

    size_t size = 0;
    if (!io_parse_size_arg(vm, argv[0], &size)) return ps_value_undefined();
    if (size == 0) {
        if (file->binary) {
            return io_make_buffer(vm, NULL, 0);
        }
        return io_return_string(vm, "", 0);
    }

    unsigned char *buf = (unsigned char *)malloc(size);
    if (!buf) return ps_value_undefined();
    size_t n = fread(buf, 1, size, file->fp);
    if (n == 0) {
        free(buf);
        if (feof(file->fp)) {
            return g_io_eof_obj ? ps_value_object(g_io_eof_obj) : ps_value_undefined();
        }
        if (ferror(file->fp)) {
            io_throw(vm, "Error", "Read error");
        }
        return ps_value_undefined();
    }
    if (ferror(file->fp)) {
        free(buf);
        io_throw(vm, "Error", "Read error");
        return ps_value_undefined();
    }
    if (file->binary) {
        PSValue out = io_make_buffer(vm, buf, n);
        free(buf);
        return out;
    }
    if (!io_check_text_data(vm, file, buf, n)) {
        free(buf);
        return ps_value_undefined();
    }
    PSValue out = io_return_string(vm, (const char *)buf, n);
    free(buf);
    return out;
}

static PSValue ps_native_file_write(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    if (this_val.type != PS_T_OBJECT || !this_val.as.object) {
        ps_vm_throw_type_error(vm, "Invalid file handle");
        return ps_value_undefined();
    }
    if (argc < 1) {
        ps_vm_throw_type_error(vm, "file.write expects (data)");
        return ps_value_undefined();
    }
    PSIOFile *file = io_get_file(vm, this_val, 0);
    if (!file || (vm && vm->has_pending_throw)) return ps_value_undefined();
    if (!file->can_write) {
        io_throw(vm, "Error", "File not open for writing");
        return ps_value_undefined();
    }
    if (file->binary) {
        if (argv[0].type != PS_T_OBJECT || !argv[0].as.object ||
            argv[0].as.object->kind != PS_OBJ_KIND_BUFFER) {
            ps_vm_throw_type_error(vm, "file.write expects (buffer)");
            return ps_value_undefined();
        }
        PSBuffer *buf = ps_buffer_from_object(argv[0].as.object);
        if (!buf) {
            ps_vm_throw_type_error(vm, "file.write expects (buffer)");
            return ps_value_undefined();
        }
        if (buf->size > 0 && buf->data) {
            size_t n = fwrite(buf->data, 1, buf->size, file->fp);
            if (n != buf->size) {
                io_throw(vm, "Error", "Write error");
                return ps_value_undefined();
            }
        }
        return ps_value_undefined();
    }
    if (argv[0].type != PS_T_STRING) {
        ps_vm_throw_type_error(vm, "file.write expects (string)");
        return ps_value_undefined();
    }
    PSString *s = argv[0].as.string;
    if (s && s->byte_len > 0) {
        size_t n = fwrite(s->utf8, 1, s->byte_len, file->fp);
        if (n != s->byte_len) {
            io_throw(vm, "Error", "Write error");
            return ps_value_undefined();
        }
    }
    return ps_value_undefined();
}

static PSValue ps_native_file_close(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)argc;
    (void)argv;
    if (this_val.type != PS_T_OBJECT || !this_val.as.object) {
        ps_vm_throw_type_error(vm, "Invalid file handle");
        return ps_value_undefined();
    }
    PSIOFile *file = io_get_file(vm, this_val, 1);
    if (!file || (vm && vm->has_pending_throw)) return ps_value_undefined();
    if (file->is_std) {
        io_throw(vm, "Error", "Cannot close standard stream");
        return ps_value_undefined();
    }
    if (file->closed) {
        return ps_value_undefined();
    }
    if (file->fp) {
        fclose(file->fp);
    }
    file->closed = 1;
    io_set_closed_property(this_val.as.object, 1);
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

    /* Create File prototype */
    g_io_file_proto = ps_object_new(vm->object_proto);
    if (g_io_file_proto) {
        PSObject *file_read_fn = ps_function_new_native(ps_native_file_read);
        PSObject *file_write_fn = ps_function_new_native(ps_native_file_write);
        PSObject *file_close_fn = ps_function_new_native(ps_native_file_close);
        if (file_read_fn) {
            ps_function_setup(file_read_fn, vm->function_proto, vm->object_proto, NULL);
            ps_object_define(g_io_file_proto,
                             ps_string_from_cstr("read"),
                             ps_value_object(file_read_fn),
                             PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
        }
        if (file_write_fn) {
            ps_function_setup(file_write_fn, vm->function_proto, vm->object_proto, NULL);
            ps_object_define(g_io_file_proto,
                             ps_string_from_cstr("write"),
                             ps_value_object(file_write_fn),
                             PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
        }
        if (file_close_fn) {
            ps_function_setup(file_close_fn, vm->function_proto, vm->object_proto, NULL);
            ps_object_define(g_io_file_proto,
                             ps_string_from_cstr("close"),
                             ps_value_object(file_close_fn),
                             PS_ATTR_DONTENUM | PS_ATTR_READONLY | PS_ATTR_DONTDELETE);
        }
    }

    /* Create a function object for print */
    PSObject *print_fn = ps_function_new_native(ps_native_print);
    if (!print_fn) return;
    ps_function_setup(print_fn, vm->function_proto, vm->object_proto, NULL);

    PSObject *sprintf_fn = ps_function_new_native(ps_native_sprintf);
    if (sprintf_fn) ps_function_setup(sprintf_fn, vm->function_proto, vm->object_proto, NULL);

    PSObject *open_fn = ps_function_new_native(ps_native_open);
    PSObject *temp_path_fn = ps_function_new_native(ps_native_temp_path);
    if (open_fn) ps_function_setup(open_fn, vm->function_proto, vm->object_proto, NULL);
    if (temp_path_fn) ps_function_setup(temp_path_fn, vm->function_proto, vm->object_proto, NULL);

    /* Attach print to Io */
    ps_object_define(
        io,
        ps_string_from_cstr("print"),
        ps_value_object(print_fn),
        PS_ATTR_NONE
    );

    if (sprintf_fn) {
        ps_object_define(io, ps_string_from_cstr("sprintf"), ps_value_object(sprintf_fn), PS_ATTR_NONE);
    }
    if (open_fn) {
        ps_object_define(io, ps_string_from_cstr("open"), ps_value_object(open_fn), PS_ATTR_NONE);
    }
    if (temp_path_fn) {
        ps_object_define(io, ps_string_from_cstr("tempPath"), ps_value_object(temp_path_fn), PS_ATTR_NONE);
    }

    ps_object_define(io,
                     ps_string_from_cstr("EOL"),
                     ps_value_string(ps_string_from_cstr("\n")),
                     PS_ATTR_READONLY | PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);

    g_io_eof_obj = ps_object_new(NULL);
    if (g_io_eof_obj) {
        ps_object_define(io,
                         ps_string_from_cstr("EOF"),
                         ps_value_object(g_io_eof_obj),
                         PS_ATTR_READONLY | PS_ATTR_DONTENUM | PS_ATTR_DONTDELETE);
    }

    PSObject *stdin_obj = io_make_file(vm, stdin, 1, 0, 1, 0,
                                       ps_string_from_cstr("<stdin>"),
                                       ps_string_from_cstr("r"));
    PSObject *stdout_obj = io_make_file(vm, stdout, 0, 1, 1, 0,
                                        ps_string_from_cstr("<stdout>"),
                                        ps_string_from_cstr("w"));
    PSObject *stderr_obj = io_make_file(vm, stderr, 0, 1, 1, 0,
                                        ps_string_from_cstr("<stderr>"),
                                        ps_string_from_cstr("w"));
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
