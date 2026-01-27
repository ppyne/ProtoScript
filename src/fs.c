#include "ps_config.h"

#if PS_ENABLE_MODULE_FS

#include "ps_eval.h"
#include "ps_function.h"
#include "ps_array.h"
#include "ps_object.h"
#include "ps_string.h"
#include "ps_value.h"
#include "ps_vm.h"

#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static PSString *fs_value_to_string(PSValue value) {
    if (value.type == PS_T_STRING) return value.as.string;
    if (value.type == PS_T_OBJECT && value.as.object &&
        value.as.object->kind == PS_OBJ_KIND_STRING &&
        value.as.object->internal) {
        PSValue *inner = (PSValue *)value.as.object->internal;
        if (inner->type == PS_T_STRING) return inner->as.string;
    }
    return NULL;
}

static char *fs_string_cstr(PSString *s) {
    if (!s) return NULL;
    if (memchr(s->utf8, '\0', s->byte_len)) return NULL;
    char *out = (char *)malloc(s->byte_len + 1);
    if (!out) return NULL;
    memcpy(out, s->utf8, s->byte_len);
    out[s->byte_len] = '\0';
    return out;
}

static char *fs_value_to_cstr(PSValue value) {
    return fs_string_cstr(fs_value_to_string(value));
}

static int fs_value_to_mode(PSValue value, mode_t *out) {
    double num = 0.0;
    if (value.type == PS_T_NUMBER) {
        num = value.as.number;
    } else if (value.type == PS_T_OBJECT && value.as.object &&
               value.as.object->kind == PS_OBJ_KIND_NUMBER &&
               value.as.object->internal) {
        PSValue *inner = (PSValue *)value.as.object->internal;
        if (inner->type != PS_T_NUMBER) return 0;
        num = inner->as.number;
    } else {
        return 0;
    }
    if (!isfinite(num) || num < 0.0 || floor(num) != num) return 0;
    if (out) *out = (mode_t)num;
    return 1;
}

static int fs_value_to_size(PSValue value, size_t *out) {
    double num = 0.0;
    if (value.type == PS_T_NUMBER) {
        num = value.as.number;
    } else if (value.type == PS_T_OBJECT && value.as.object &&
               value.as.object->kind == PS_OBJ_KIND_NUMBER &&
               value.as.object->internal) {
        PSValue *inner = (PSValue *)value.as.object->internal;
        if (inner->type != PS_T_NUMBER) return 0;
        num = inner->as.number;
    } else {
        return 0;
    }
    if (!isfinite(num) || num < 0.0 || floor(num) != num) return 0;
    if (num > (double)SIZE_MAX) return 0;
    if (out) *out = (size_t)num;
    return 1;
}

static PSObject *fs_make_array(PSVM *vm) {
    PSObject *arr = ps_object_new(vm && vm->array_proto ? vm->array_proto
                                                        : (vm ? vm->object_proto : NULL));
    if (!arr) return NULL;
    arr->kind = PS_OBJ_KIND_ARRAY;
    (void)ps_array_init(arr);
    return arr;
}

static void fs_finalize_array(PSObject *arr, size_t count) {
    if (!arr) return;
    (void)ps_array_set_length_internal(arr, count);
}

static PSValue ps_native_fs_chmod(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    if (argc < 2) return ps_value_boolean(0);
    char *path = fs_value_to_cstr(argv[0]);
    if (!path) return ps_value_boolean(0);
    mode_t mode = 0;
    if (!fs_value_to_mode(argv[1], &mode)) {
        free(path);
        return ps_value_boolean(0);
    }
    int ok = (chmod(path, mode) == 0);
    free(path);
    return ps_value_boolean(ok);
}

static PSValue ps_native_fs_exists(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    if (argc < 1) return ps_value_boolean(0);
    char *path = fs_value_to_cstr(argv[0]);
    if (!path) return ps_value_boolean(0);
    struct stat st;
    int ok = (lstat(path, &st) == 0);
    free(path);
    return ps_value_boolean(ok);
}

static PSValue ps_native_fs_size(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    if (argc < 1) return ps_value_undefined();
    char *path = fs_value_to_cstr(argv[0]);
    if (!path) return ps_value_undefined();
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        free(path);
        return ps_value_undefined();
    }
    PSValue out = ps_value_number((double)st.st_size);
    free(path);
    return out;
}

static PSValue ps_native_fs_is_dir(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    if (argc < 1) return ps_value_boolean(0);
    char *path = fs_value_to_cstr(argv[0]);
    if (!path) return ps_value_boolean(0);
    struct stat st;
    int ok = (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
    free(path);
    return ps_value_boolean(ok);
}

static PSValue ps_native_fs_is_file(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    if (argc < 1) return ps_value_boolean(0);
    char *path = fs_value_to_cstr(argv[0]);
    if (!path) return ps_value_boolean(0);
    struct stat st;
    int ok = (stat(path, &st) == 0 && S_ISREG(st.st_mode));
    free(path);
    return ps_value_boolean(ok);
}

static PSValue ps_native_fs_is_symlink(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    if (argc < 1) return ps_value_boolean(0);
    char *path = fs_value_to_cstr(argv[0]);
    if (!path) return ps_value_boolean(0);
    struct stat st;
    int ok = (lstat(path, &st) == 0 && S_ISLNK(st.st_mode));
    free(path);
    return ps_value_boolean(ok);
}

static int fs_check_access(const char *path, int mode) {
#if defined(AT_EACCESS)
    return faccessat(AT_FDCWD, path, mode, AT_EACCESS) == 0;
#else
    return access(path, mode) == 0;
#endif
}

static PSValue ps_native_fs_is_executable(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    if (argc < 1) return ps_value_boolean(0);
    char *path = fs_value_to_cstr(argv[0]);
    if (!path) return ps_value_boolean(0);
    int ok = fs_check_access(path, X_OK);
    free(path);
    return ps_value_boolean(ok);
}

static PSValue ps_native_fs_is_readable(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    if (argc < 1) return ps_value_boolean(0);
    char *path = fs_value_to_cstr(argv[0]);
    if (!path) return ps_value_boolean(0);
    int ok = fs_check_access(path, R_OK);
    free(path);
    return ps_value_boolean(ok);
}

static PSValue ps_native_fs_is_writable(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    if (argc < 1) return ps_value_boolean(0);
    char *path = fs_value_to_cstr(argv[0]);
    if (!path) return ps_value_boolean(0);
    int ok = fs_check_access(path, W_OK);
    free(path);
    return ps_value_boolean(ok);
}

static PSValue ps_native_fs_ls(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    PSObject *arr = fs_make_array(vm);
    if (!arr) return ps_value_undefined();
    if (argc < 1) {
        fs_finalize_array(arr, 0);
        return ps_value_object(arr);
    }
    char *path = fs_value_to_cstr(argv[0]);
    if (!path) {
        fs_finalize_array(arr, 0);
        return ps_value_object(arr);
    }
    int include_all = 0;
    if (argc > 1) include_all = ps_to_boolean(vm, argv[1]);
    size_t limit = 0;
    int has_limit = 0;
    if (argc > 2) {
        has_limit = 1;
        if (!fs_value_to_size(argv[2], &limit)) {
            fs_finalize_array(arr, 0);
            return ps_value_object(arr);
        }
    }

    DIR *dir = opendir(path);
    free(path);
    if (!dir) {
        fs_finalize_array(arr, 0);
        return ps_value_object(arr);
    }

    size_t count = 0;
    struct dirent *ent = NULL;
    while ((ent = readdir(dir)) != NULL) {
        if (has_limit && limit > 0 && count >= limit) break;
        if (!include_all && ent->d_name[0] == '.') continue;
        PSString *name = ps_string_from_cstr(ent->d_name);
        if (!name) continue;
        char idx_buf[32];
        snprintf(idx_buf, sizeof(idx_buf), "%zu", count);
        ps_object_define(arr, ps_string_from_cstr(idx_buf),
                         ps_value_string(name), PS_ATTR_NONE);
        count++;
    }
    closedir(dir);
    fs_finalize_array(arr, count);
    return ps_value_object(arr);
}

static PSValue ps_native_fs_mkdir(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    if (argc < 1) return ps_value_boolean(0);
    char *path = fs_value_to_cstr(argv[0]);
    if (!path) return ps_value_boolean(0);
    int ok = (mkdir(path, 0777) == 0);
    free(path);
    return ps_value_boolean(ok);
}

static int fs_stat_is_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode);
}

static int fs_stat_is_reg(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISREG(st.st_mode);
}

static PSValue ps_native_fs_rm(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    if (argc < 1) return ps_value_boolean(0);
    char *path = fs_value_to_cstr(argv[0]);
    if (!path) return ps_value_boolean(0);
    if (fs_stat_is_dir(path)) {
        free(path);
        return ps_value_boolean(0);
    }
    int ok = (unlink(path) == 0);
    free(path);
    return ps_value_boolean(ok);
}

static PSValue ps_native_fs_rmdir(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    if (argc < 1) return ps_value_boolean(0);
    char *path = fs_value_to_cstr(argv[0]);
    if (!path) return ps_value_boolean(0);
    int ok = (rmdir(path) == 0);
    free(path);
    return ps_value_boolean(ok);
}

static PSValue ps_native_fs_pwd(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    (void)argc;
    (void)argv;
    char *cwd = getcwd(NULL, 0);
    if (!cwd) return ps_value_undefined();
    PSValue out = ps_value_string(ps_string_from_cstr(cwd));
    free(cwd);
    return out;
}

static PSValue ps_native_fs_cd(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    if (argc < 1) return ps_value_boolean(0);
    char *path = fs_value_to_cstr(argv[0]);
    if (!path) return ps_value_boolean(0);
    int ok = (chdir(path) == 0);
    free(path);
    return ps_value_boolean(ok);
}

static PSValue ps_native_fs_path_info(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    if (argc < 1) return ps_value_undefined();
    char *path = fs_value_to_cstr(argv[0]);
    if (!path) return ps_value_undefined();

    const char *last_slash = strrchr(path, '/');
    const char *base = last_slash ? last_slash + 1 : path;
    size_t dir_len = 0;
    if (last_slash) {
        dir_len = (last_slash == path) ? 1 : (size_t)(last_slash - path);
    }

    const char *last_dot = strrchr(base, '.');
    size_t base_len = strlen(base);
    size_t file_len = base_len;
    const char *ext = "";
    if (last_dot && last_dot != base) {
        file_len = (size_t)(last_dot - base);
        ext = last_dot + 1;
    }

    char *dir_buf = (char *)malloc(dir_len + 1);
    if (!dir_buf) {
        free(path);
        return ps_value_undefined();
    }
    if (dir_len > 0) memcpy(dir_buf, path, dir_len);
    dir_buf[dir_len] = '\0';

    char *file_buf = (char *)malloc(file_len + 1);
    if (!file_buf) {
        free(dir_buf);
        free(path);
        return ps_value_undefined();
    }
    if (file_len > 0) memcpy(file_buf, base, file_len);
    file_buf[file_len] = '\0';

    PSObject *obj = ps_object_new(vm ? vm->object_proto : NULL);
    if (!obj) {
        free(file_buf);
        free(dir_buf);
        free(path);
        return ps_value_undefined();
    }

    ps_object_define(obj, ps_string_from_cstr("dirname"),
                     ps_value_string(ps_string_from_cstr(dir_buf)),
                     PS_ATTR_NONE);
    ps_object_define(obj, ps_string_from_cstr("basename"),
                     ps_value_string(ps_string_from_cstr(base)),
                     PS_ATTR_NONE);
    ps_object_define(obj, ps_string_from_cstr("filename"),
                     ps_value_string(ps_string_from_cstr(file_buf)),
                     PS_ATTR_NONE);
    ps_object_define(obj, ps_string_from_cstr("extension"),
                     ps_value_string(ps_string_from_cstr(ext)),
                     PS_ATTR_NONE);

    free(file_buf);
    free(dir_buf);
    free(path);
    return ps_value_object(obj);
}

static int fs_copy_fd(int src_fd, int dst_fd) {
    char buf[8192];
    for (;;) {
        ssize_t n = read(src_fd, buf, sizeof(buf));
        if (n == 0) return 1;
        if (n < 0) return 0;
        ssize_t off = 0;
        while (off < n) {
            ssize_t w = write(dst_fd, buf + off, (size_t)(n - off));
            if (w <= 0) return 0;
            off += w;
        }
    }
}

static char *fs_make_temp_path(const char *dir) {
    const char *name = ".pscpXXXXXX";
    size_t dir_len = strlen(dir);
    size_t name_len = strlen(name);
    size_t total = dir_len + 1 + name_len + 1;
    char *tmpl = (char *)malloc(total);
    if (!tmpl) return NULL;
    snprintf(tmpl, total, "%s/%s", dir, name);
    int fd = mkstemp(tmpl);
    if (fd < 0) {
        free(tmpl);
        return NULL;
    }
    close(fd);
    return tmpl;
}

static PSValue ps_native_fs_cp(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    if (argc < 2) return ps_value_boolean(0);
    char *src = fs_value_to_cstr(argv[0]);
    char *dst = fs_value_to_cstr(argv[1]);
    if (!src || !dst) {
        free(src);
        free(dst);
        return ps_value_boolean(0);
    }

    struct stat st;
    if (stat(src, &st) != 0 || !S_ISREG(st.st_mode)) {
        free(src);
        free(dst);
        return ps_value_boolean(0);
    }
    if (fs_stat_is_dir(dst)) {
        free(src);
        free(dst);
        return ps_value_boolean(0);
    }

    const char *slash = strrchr(dst, '/');
    const char *dir = ".";
    char *dir_buf = NULL;
    if (slash) {
        size_t dir_len = (slash == dst) ? 1 : (size_t)(slash - dst);
        dir_buf = (char *)malloc(dir_len + 1);
        if (!dir_buf) {
            free(src);
            free(dst);
            return ps_value_boolean(0);
        }
        memcpy(dir_buf, dst, dir_len);
        dir_buf[dir_len] = '\0';
        dir = dir_buf;
    }

    char *tmp_path = fs_make_temp_path(dir);
    if (!tmp_path) {
        free(dir_buf);
        free(src);
        free(dst);
        return ps_value_boolean(0);
    }

    int src_fd = open(src, O_RDONLY);
    if (src_fd < 0) {
        unlink(tmp_path);
        free(tmp_path);
        free(dir_buf);
        free(src);
        free(dst);
        return ps_value_boolean(0);
    }
    int dst_fd = open(tmp_path, O_WRONLY | O_TRUNC);
    if (dst_fd < 0) {
        close(src_fd);
        unlink(tmp_path);
        free(tmp_path);
        free(dir_buf);
        free(src);
        free(dst);
        return ps_value_boolean(0);
    }

    int ok = fs_copy_fd(src_fd, dst_fd);
    close(src_fd);
    close(dst_fd);

    if (ok) {
        chmod(tmp_path, st.st_mode & 0777);
        if (rename(tmp_path, dst) != 0) ok = 0;
    }
    if (!ok) unlink(tmp_path);

    free(tmp_path);
    free(dir_buf);
    free(src);
    free(dst);
    return ps_value_boolean(ok);
}

static PSValue ps_native_fs_mv(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)vm;
    (void)this_val;
    if (argc < 2) return ps_value_boolean(0);
    char *src = fs_value_to_cstr(argv[0]);
    char *dst = fs_value_to_cstr(argv[1]);
    if (!src || !dst) {
        free(src);
        free(dst);
        return ps_value_boolean(0);
    }

    if (!fs_stat_is_reg(src)) {
        free(src);
        free(dst);
        return ps_value_boolean(0);
    }

    char *final_dst = NULL;
    if (!strchr(dst, '/')) {
        const char *slash = strrchr(src, '/');
        if (slash) {
            size_t dir_len = (slash == src) ? 1 : (size_t)(slash - src);
            size_t name_len = strlen(dst);
            final_dst = (char *)malloc(dir_len + 1 + name_len + 1);
            if (!final_dst) {
                free(src);
                free(dst);
                return ps_value_boolean(0);
            }
            memcpy(final_dst, src, dir_len);
            final_dst[dir_len] = '\0';
            snprintf(final_dst + dir_len, name_len + 2, "/%s", dst);
        }
    }

    const char *target = final_dst ? final_dst : dst;
    int ok = (rename(src, target) == 0);

    free(final_dst);
    free(src);
    free(dst);
    return ps_value_boolean(ok);
}

void ps_fs_init(PSVM *vm) {
    if (!vm || !vm->global) return;

    PSObject *fs = ps_object_new(NULL);
    if (!fs) return;

    PSObject *chmod_fn = ps_function_new_native(ps_native_fs_chmod);
    PSObject *cp_fn = ps_function_new_native(ps_native_fs_cp);
    PSObject *exists_fn = ps_function_new_native(ps_native_fs_exists);
    PSObject *size_fn = ps_function_new_native(ps_native_fs_size);
    PSObject *is_dir_fn = ps_function_new_native(ps_native_fs_is_dir);
    PSObject *is_file_fn = ps_function_new_native(ps_native_fs_is_file);
    PSObject *is_symlink_fn = ps_function_new_native(ps_native_fs_is_symlink);
    PSObject *is_exec_fn = ps_function_new_native(ps_native_fs_is_executable);
    PSObject *is_read_fn = ps_function_new_native(ps_native_fs_is_readable);
    PSObject *is_write_fn = ps_function_new_native(ps_native_fs_is_writable);
    PSObject *ls_fn = ps_function_new_native(ps_native_fs_ls);
    PSObject *mkdir_fn = ps_function_new_native(ps_native_fs_mkdir);
    PSObject *mv_fn = ps_function_new_native(ps_native_fs_mv);
    PSObject *path_info_fn = ps_function_new_native(ps_native_fs_path_info);
    PSObject *cd_fn = ps_function_new_native(ps_native_fs_cd);
    PSObject *pwd_fn = ps_function_new_native(ps_native_fs_pwd);
    PSObject *rmdir_fn = ps_function_new_native(ps_native_fs_rmdir);
    PSObject *rm_fn = ps_function_new_native(ps_native_fs_rm);

    if (chmod_fn) ps_function_setup(chmod_fn, vm->function_proto, vm->object_proto, NULL);
    if (cp_fn) ps_function_setup(cp_fn, vm->function_proto, vm->object_proto, NULL);
    if (exists_fn) ps_function_setup(exists_fn, vm->function_proto, vm->object_proto, NULL);
    if (size_fn) ps_function_setup(size_fn, vm->function_proto, vm->object_proto, NULL);
    if (is_dir_fn) ps_function_setup(is_dir_fn, vm->function_proto, vm->object_proto, NULL);
    if (is_file_fn) ps_function_setup(is_file_fn, vm->function_proto, vm->object_proto, NULL);
    if (is_symlink_fn) ps_function_setup(is_symlink_fn, vm->function_proto, vm->object_proto, NULL);
    if (is_exec_fn) ps_function_setup(is_exec_fn, vm->function_proto, vm->object_proto, NULL);
    if (is_read_fn) ps_function_setup(is_read_fn, vm->function_proto, vm->object_proto, NULL);
    if (is_write_fn) ps_function_setup(is_write_fn, vm->function_proto, vm->object_proto, NULL);
    if (ls_fn) ps_function_setup(ls_fn, vm->function_proto, vm->object_proto, NULL);
    if (mkdir_fn) ps_function_setup(mkdir_fn, vm->function_proto, vm->object_proto, NULL);
    if (mv_fn) ps_function_setup(mv_fn, vm->function_proto, vm->object_proto, NULL);
    if (path_info_fn) ps_function_setup(path_info_fn, vm->function_proto, vm->object_proto, NULL);
    if (cd_fn) ps_function_setup(cd_fn, vm->function_proto, vm->object_proto, NULL);
    if (pwd_fn) ps_function_setup(pwd_fn, vm->function_proto, vm->object_proto, NULL);
    if (rmdir_fn) ps_function_setup(rmdir_fn, vm->function_proto, vm->object_proto, NULL);
    if (rm_fn) ps_function_setup(rm_fn, vm->function_proto, vm->object_proto, NULL);

    if (chmod_fn) ps_object_define(fs, ps_string_from_cstr("chmod"), ps_value_object(chmod_fn), PS_ATTR_NONE);
    if (cp_fn) ps_object_define(fs, ps_string_from_cstr("cp"), ps_value_object(cp_fn), PS_ATTR_NONE);
    if (exists_fn) ps_object_define(fs, ps_string_from_cstr("exists"), ps_value_object(exists_fn), PS_ATTR_NONE);
    if (size_fn) ps_object_define(fs, ps_string_from_cstr("size"), ps_value_object(size_fn), PS_ATTR_NONE);
    if (is_dir_fn) ps_object_define(fs, ps_string_from_cstr("isDir"), ps_value_object(is_dir_fn), PS_ATTR_NONE);
    if (is_file_fn) ps_object_define(fs, ps_string_from_cstr("isFile"), ps_value_object(is_file_fn), PS_ATTR_NONE);
    if (is_symlink_fn) ps_object_define(fs, ps_string_from_cstr("isSymlink"), ps_value_object(is_symlink_fn), PS_ATTR_NONE);
    if (is_exec_fn) ps_object_define(fs, ps_string_from_cstr("isExecutable"), ps_value_object(is_exec_fn), PS_ATTR_NONE);
    if (is_read_fn) ps_object_define(fs, ps_string_from_cstr("isReadable"), ps_value_object(is_read_fn), PS_ATTR_NONE);
    if (is_write_fn) ps_object_define(fs, ps_string_from_cstr("isWritable"), ps_value_object(is_write_fn), PS_ATTR_NONE);
    if (ls_fn) ps_object_define(fs, ps_string_from_cstr("ls"), ps_value_object(ls_fn), PS_ATTR_NONE);
    if (mkdir_fn) ps_object_define(fs, ps_string_from_cstr("mkdir"), ps_value_object(mkdir_fn), PS_ATTR_NONE);
    if (mv_fn) ps_object_define(fs, ps_string_from_cstr("mv"), ps_value_object(mv_fn), PS_ATTR_NONE);
    if (path_info_fn) ps_object_define(fs, ps_string_from_cstr("pathInfo"), ps_value_object(path_info_fn), PS_ATTR_NONE);
    if (cd_fn) ps_object_define(fs, ps_string_from_cstr("cd"), ps_value_object(cd_fn), PS_ATTR_NONE);
    if (pwd_fn) ps_object_define(fs, ps_string_from_cstr("pwd"), ps_value_object(pwd_fn), PS_ATTR_NONE);
    if (rmdir_fn) ps_object_define(fs, ps_string_from_cstr("rmdir"), ps_value_object(rmdir_fn), PS_ATTR_NONE);
    if (rm_fn) ps_object_define(fs, ps_string_from_cstr("rm"), ps_value_object(rm_fn), PS_ATTR_NONE);

    ps_object_define(vm->global, ps_string_from_cstr("Fs"), ps_value_object(fs), PS_ATTR_NONE);
}

#endif /* PS_ENABLE_MODULE_FS */
