#include "ps_config.h"

#if PS_ENABLE_MODULE_IMG

#include "ps_buffer.h"
#include "ps_eval.h"
#include "ps_function.h"
#include "ps_img.h"
#include "ps_img_resample.h"
#include "ps_object.h"
#include "ps_string.h"
#include "ps_value.h"
#include "ps_vm.h"

#include <stdio.h>
#include <jpeglib.h>
#include <png.h>
#include <limits.h>
#include <math.h>
#include <setjmp.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t offset;
} PSPngReadState;

typedef struct {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
} PSJpegError;

typedef struct {
    uint8_t *data;
    size_t size;
    size_t capacity;
} PSPngWriteState;

static size_t g_img_live = 0;

static void img_throw(PSVM *vm, const char *name, const char *message) {
    if (!vm) return;
    vm->pending_throw = ps_vm_make_error(vm, name ? name : "Error", message ? message : "");
    vm->has_pending_throw = 1;
}

PSImageHandle *ps_img_handle_new(PSVM *vm, size_t byte_len) {
    if (PS_IMG_MAX_IMAGES > 0 && g_img_live >= PS_IMG_MAX_IMAGES) {
        if (vm) ps_gc_collect(vm);
        if (g_img_live >= PS_IMG_MAX_IMAGES) {
            img_throw(vm, "ResourceLimitError", "Image limit exceeded");
            return NULL;
        }
    }
    PSImageHandle *handle = (PSImageHandle *)malloc(sizeof(PSImageHandle));
    if (!handle) {
        img_throw(vm, "ResourceLimitError", "Out of memory");
        return NULL;
    }
    handle->byte_len = byte_len;
    g_img_live++;
    return handle;
}

void ps_img_handle_release(PSImageHandle *handle) {
    if (!handle) return;
    if (g_img_live > 0) g_img_live--;
    free(handle);
}

static int img_string_equals(PSString *s, const char *lit) {
    size_t len = strlen(lit);
    return s && s->byte_len == len && memcmp(s->utf8, lit, len) == 0;
}

static PSString *img_value_to_string(PSValue value) {
    if (value.type == PS_T_STRING) return value.as.string;
    if (value.type == PS_T_OBJECT && value.as.object &&
        value.as.object->kind == PS_OBJ_KIND_STRING &&
        value.as.object->internal) {
        PSValue *inner = (PSValue *)value.as.object->internal;
        if (inner->type == PS_T_STRING) return inner->as.string;
    }
    return NULL;
}

static int img_parse_dim(PSVM *vm, PSValue value, int *out) {
    double num = ps_to_number(vm, value);
    if (vm && vm->has_pending_throw) return 0;
    if (!isfinite(num) || num <= 0.0 || floor(num) != num || num > (double)INT_MAX) {
        img_throw(vm, "ArgumentError", "Invalid image dimension");
        return 0;
    }
    if (out) *out = (int)num;
    return 1;
}

static int img_check_limits(PSVM *vm, int width, int height) {
    if (width <= 0 || height <= 0) {
        img_throw(vm, "ArgumentError", "Invalid image size");
        return 0;
    }
    if (width > PS_IMG_MAX_WIDTH || height > PS_IMG_MAX_HEIGHT) {
        img_throw(vm, "ResourceLimitError", "Image size exceeds limits");
        return 0;
    }
    return 1;
}

static int img_compute_byte_len(PSVM *vm, int width, int height, size_t *out_len) {
    if (width <= 0 || height <= 0) {
        img_throw(vm, "ArgumentError", "Invalid image size");
        return 0;
    }
    if ((size_t)width > SIZE_MAX / 4 ||
        (size_t)height > SIZE_MAX / ((size_t)width * 4)) {
        img_throw(vm, "ResourceLimitError", "Image size exceeds addressable memory");
        return 0;
    }
    if (out_len) *out_len = (size_t)width * (size_t)height * 4;
    return 1;
}

static PSBuffer *img_require_buffer(PSVM *vm, PSValue value) {
    if (value.type != PS_T_OBJECT || !value.as.object ||
        value.as.object->kind != PS_OBJ_KIND_BUFFER) {
        img_throw(vm, "ArgumentError", "Expected Buffer");
        return NULL;
    }
    PSBuffer *buf = ps_buffer_from_object(value.as.object);
    if (!buf) {
        img_throw(vm, "ArgumentError", "Expected Buffer");
        return NULL;
    }
    return buf;
}

static int img_extract_image(PSVM *vm, PSValue value, int *out_w, int *out_h, PSBuffer **out_buf) {
    if (value.type != PS_T_OBJECT || !value.as.object) {
        img_throw(vm, "ArgumentError", "Expected Image object");
        return 0;
    }
    PSObject *obj = value.as.object;
    int found = 0;
    PSValue width_val = ps_object_get(obj, ps_string_from_cstr("width"), &found);
    if (!found) {
        img_throw(vm, "ArgumentError", "Missing image width");
        return 0;
    }
    PSValue height_val = ps_object_get(obj, ps_string_from_cstr("height"), &found);
    if (!found) {
        img_throw(vm, "ArgumentError", "Missing image height");
        return 0;
    }
    PSValue data_val = ps_object_get(obj, ps_string_from_cstr("data"), &found);
    if (!found) {
        img_throw(vm, "ArgumentError", "Missing image data");
        return 0;
    }
    int width = 0;
    int height = 0;
    if (!img_parse_dim(vm, width_val, &width) ||
        !img_parse_dim(vm, height_val, &height)) {
        return 0;
    }
    if (!img_check_limits(vm, width, height)) return 0;
    PSBuffer *buf = img_require_buffer(vm, data_val);
    if (!buf) return 0;
    size_t expected = 0;
    if (!img_compute_byte_len(vm, width, height, &expected)) return 0;
    if (buf->size != expected) {
        img_throw(vm, "ArgumentError", "Image data length mismatch");
        return 0;
    }
    if (out_w) *out_w = width;
    if (out_h) *out_h = height;
    if (out_buf) *out_buf = buf;
    return 1;
}

static void img_png_write(png_structp png_ptr, png_bytep data, png_size_t length) {
    PSPngWriteState *state = (PSPngWriteState *)png_get_io_ptr(png_ptr);
    if (!state) return;
    size_t needed = state->size + length;
    if (needed > state->capacity) {
        size_t new_cap = state->capacity ? state->capacity : 1024;
        while (new_cap < needed) {
            new_cap *= 2;
        }
        uint8_t *next = (uint8_t *)realloc(state->data, new_cap);
        if (!next) {
            png_error(png_ptr, "out of memory");
            return;
        }
        state->data = next;
        state->capacity = new_cap;
    }
    memcpy(state->data + state->size, data, length);
    state->size += length;
}

static void img_png_flush(png_structp png_ptr) {
    (void)png_ptr;
}

static int img_parse_mode(PSVM *vm, PSValue value, PsImgResampleInterpolation *out) {
    if (value.type == PS_T_UNDEFINED || value.type == PS_T_NULL) {
        if (out) *out = PS_IMG_RESAMPLE_INTERP_CUBIC;
        return 1;
    }
    PSString *s = img_value_to_string(value);
    if (!s) {
        img_throw(vm, "ArgumentError", "Invalid resample mode");
        return 0;
    }
    if (img_string_equals(s, "none")) {
        if (out) *out = PS_IMG_RESAMPLE_INTERP_NONE;
        return 1;
    }
    if (img_string_equals(s, "linear")) {
        if (out) *out = PS_IMG_RESAMPLE_INTERP_LINEAR;
        return 1;
    }
    if (img_string_equals(s, "cubic")) {
        if (out) *out = PS_IMG_RESAMPLE_INTERP_CUBIC;
        return 1;
    }
    if (img_string_equals(s, "nohalo")) {
        if (out) *out = PS_IMG_RESAMPLE_INTERP_NOHALO;
        return 1;
    }
    if (img_string_equals(s, "lohalo")) {
        if (out) *out = PS_IMG_RESAMPLE_INTERP_LOHALO;
        return 1;
    }
    img_throw(vm, "ArgumentError", "Invalid resample mode");
    return 0;
}

static int img_timing_enabled(void) {
    static int checked = 0;
    static int enabled = 0;
    if (!checked) {
        const char *env = getenv("PS_IMG_TIMING");
        enabled = (env && env[0] == '1') ? 1 : 0;
        checked = 1;
    }
    return enabled;
}

static uint64_t img_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000ULL);
}

static PSValue img_make_image_object(PSVM *vm, int width, int height, PSObject *buf_obj, size_t byte_len) {
    if (!vm) return ps_value_undefined();
    PSImageHandle *handle = ps_img_handle_new(vm, byte_len);
    if (!handle) return ps_value_undefined();
    PSObject *img = ps_object_new(vm->object_proto);
    if (!img) {
        ps_img_handle_release(handle);
        return ps_value_undefined();
    }
    img->kind = PS_OBJ_KIND_IMAGE;
    img->internal = handle;
    ps_object_define(img, ps_string_from_cstr("width"), ps_value_number((double)width), PS_ATTR_NONE);
    ps_object_define(img, ps_string_from_cstr("height"), ps_value_number((double)height), PS_ATTR_NONE);
    ps_object_define(img, ps_string_from_cstr("data"), ps_value_object(buf_obj), PS_ATTR_NONE);
    return ps_value_object(img);
}

static void img_png_read(png_structp png_ptr, png_bytep out_bytes, png_size_t byte_count) {
    PSPngReadState *state = (PSPngReadState *)png_get_io_ptr(png_ptr);
    if (!state || state->offset + byte_count > state->size) {
        png_error(png_ptr, "read overflow");
        return;
    }
    memcpy(out_bytes, state->data + state->offset, byte_count);
    state->offset += byte_count;
}

static PSValue ps_native_img_decode_png(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (argc < 1) {
        img_throw(vm, "ArgumentError", "Image.decodePNG expects (buffer)");
        return ps_value_undefined();
    }
    PSBuffer *buf = img_require_buffer(vm, argv[0]);
    if (!buf) return ps_value_undefined();
    if (!buf->data || buf->size == 0) {
        img_throw(vm, "DecodeError", "Invalid PNG data");
        return ps_value_undefined();
    }
    uint64_t t0 = 0;
    if (img_timing_enabled()) t0 = img_now_ms();

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        img_throw(vm, "ResourceLimitError", "PNG decoder unavailable");
        return ps_value_undefined();
    }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        img_throw(vm, "ResourceLimitError", "PNG decoder unavailable");
        return ps_value_undefined();
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        img_throw(vm, "DecodeError", "Invalid PNG data");
        return ps_value_undefined();
    }

    PSPngReadState state = { buf->data, buf->size, 0 };
    png_set_read_fn(png, &state, img_png_read);
    png_read_info(png, info);

    png_uint_32 width = 0;
    png_uint_32 height = 0;
    int bit_depth = 0;
    int color_type = 0;
    int interlace_type = 0;
    png_get_IHDR(png, info, &width, &height, &bit_depth, &color_type, &interlace_type, NULL, NULL);

    if (width > (png_uint_32)INT_MAX || height > (png_uint_32)INT_MAX) {
        png_destroy_read_struct(&png, &info, NULL);
        img_throw(vm, "ResourceLimitError", "Image size exceeds limits");
        return ps_value_undefined();
    }
    if (!img_check_limits(vm, (int)width, (int)height)) {
        png_destroy_read_struct(&png, &info, NULL);
        return ps_value_undefined();
    }

    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
    if (!(color_type & PNG_COLOR_MASK_ALPHA)) png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    if (interlace_type != PNG_INTERLACE_NONE) png_set_interlace_handling(png);

    png_read_update_info(png, info);

    png_size_t rowbytes = png_get_rowbytes(png, info);
    size_t expected = 0;
    if (!img_compute_byte_len(vm, (int)width, (int)height, &expected)) {
        png_destroy_read_struct(&png, &info, NULL);
        return ps_value_undefined();
    }
    if (rowbytes != (png_size_t)(width * 4)) {
        png_destroy_read_struct(&png, &info, NULL);
        img_throw(vm, "DecodeError", "Unsupported PNG layout");
        return ps_value_undefined();
    }

    PSObject *buf_obj = ps_buffer_new(vm, expected);
    if (!buf_obj) {
        png_destroy_read_struct(&png, &info, NULL);
        img_throw(vm, "ResourceLimitError", "Out of memory");
        return ps_value_undefined();
    }
    PSBuffer *out_buf = ps_buffer_from_object(buf_obj);
    if (!out_buf || !out_buf->data) {
        png_destroy_read_struct(&png, &info, NULL);
        img_throw(vm, "ResourceLimitError", "Out of memory");
        return ps_value_undefined();
    }

    png_bytep *rows = (png_bytep *)malloc(sizeof(png_bytep) * height);
    if (!rows) {
        png_destroy_read_struct(&png, &info, NULL);
        img_throw(vm, "ResourceLimitError", "Out of memory");
        return ps_value_undefined();
    }
    for (png_uint_32 y = 0; y < height; y++) {
        rows[y] = out_buf->data + y * rowbytes;
    }
    png_read_image(png, rows);
    png_read_end(png, NULL);
    free(rows);
    png_destroy_read_struct(&png, &info, NULL);

    ps_gc_root_push(vm, PS_GC_ROOT_OBJECT, buf_obj);
    PSValue out = img_make_image_object(vm, (int)width, (int)height, buf_obj, expected);
    ps_gc_root_pop(vm, 1);
    if (img_timing_enabled()) {
        uint64_t dt = img_now_ms() - t0;
        fprintf(stderr,
                "img_timing decodePNG %ux%u input=%zuB output=%zuB %llums\n",
                (unsigned)width,
                (unsigned)height,
                buf->size,
                expected,
                (unsigned long long)dt);
    }
    return out;
}

static PSValue ps_native_img_detect_format(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (argc < 1) {
        img_throw(vm, "ArgumentError", "Image.detectFormat expects (buffer)");
        return ps_value_undefined();
    }
    PSBuffer *buf = img_require_buffer(vm, argv[0]);
    if (!buf) return ps_value_undefined();
    if (!buf->data || buf->size == 0) return ps_value_null();

    if (buf->size >= 8) {
        const uint8_t sig_png[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
        if (memcmp(buf->data, sig_png, sizeof(sig_png)) == 0) {
            return ps_value_string(ps_string_from_cstr("png"));
        }
    }
    if (buf->size >= 3) {
        if (buf->data[0] == 0xFF && buf->data[1] == 0xD8 && buf->data[2] == 0xFF) {
            return ps_value_string(ps_string_from_cstr("jpeg"));
        }
    }
    return ps_value_null();
}

static void img_jpeg_error_exit(j_common_ptr cinfo) {
    PSJpegError *err = (PSJpegError *)cinfo->err;
    longjmp(err->setjmp_buffer, 1);
}

static PSValue ps_native_img_decode_jpeg(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (argc < 1) {
        img_throw(vm, "ArgumentError", "Image.decodeJPEG expects (buffer)");
        return ps_value_undefined();
    }
    PSBuffer *buf = img_require_buffer(vm, argv[0]);
    if (!buf) return ps_value_undefined();
    if (!buf->data || buf->size == 0) {
        img_throw(vm, "DecodeError", "Invalid JPEG data");
        return ps_value_undefined();
    }
    uint64_t t0 = 0;
    if (img_timing_enabled()) t0 = img_now_ms();

    struct jpeg_decompress_struct cinfo;
    PSJpegError jerr;
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = img_jpeg_error_exit;
    if (setjmp(jerr.setjmp_buffer)) {
        jpeg_destroy_decompress(&cinfo);
        img_throw(vm, "DecodeError", "Invalid JPEG data");
        return ps_value_undefined();
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, (unsigned char *)buf->data, buf->size);
    jpeg_read_header(&cinfo, TRUE);
    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);

    if (cinfo.output_width > (unsigned int)INT_MAX ||
        cinfo.output_height > (unsigned int)INT_MAX) {
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        img_throw(vm, "ResourceLimitError", "Image size exceeds limits");
        return ps_value_undefined();
    }
    int width = (int)cinfo.output_width;
    int height = (int)cinfo.output_height;
    int comps = (int)cinfo.output_components;

    if (!img_check_limits(vm, width, height)) {
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        return ps_value_undefined();
    }

    if (comps != 3 && comps != 1) {
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        img_throw(vm, "DecodeError", "Unsupported JPEG format");
        return ps_value_undefined();
    }

    size_t expected = 0;
    if (!img_compute_byte_len(vm, width, height, &expected)) {
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        return ps_value_undefined();
    }

    PSObject *buf_obj = ps_buffer_new(vm, expected);
    if (!buf_obj) {
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        img_throw(vm, "ResourceLimitError", "Out of memory");
        return ps_value_undefined();
    }
    PSBuffer *out_buf = ps_buffer_from_object(buf_obj);
    if (!out_buf || !out_buf->data) {
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        img_throw(vm, "ResourceLimitError", "Out of memory");
        return ps_value_undefined();
    }

    size_t row_stride = (size_t)width * (size_t)comps;
    JSAMPARRAY row = (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo, JPOOL_IMAGE, row_stride, 1);
    int row_idx = 0;
    while (cinfo.output_scanline < cinfo.output_height) {
        jpeg_read_scanlines(&cinfo, row, 1);
        uint8_t *dst = out_buf->data + (size_t)row_idx * (size_t)width * 4;
        if (comps == 3) {
            for (int x = 0; x < width; x++) {
                dst[x * 4 + 0] = row[0][x * 3 + 0];
                dst[x * 4 + 1] = row[0][x * 3 + 1];
                dst[x * 4 + 2] = row[0][x * 3 + 2];
                dst[x * 4 + 3] = 255;
            }
        } else {
            for (int x = 0; x < width; x++) {
                uint8_t v = row[0][x];
                dst[x * 4 + 0] = v;
                dst[x * 4 + 1] = v;
                dst[x * 4 + 2] = v;
                dst[x * 4 + 3] = 255;
            }
        }
        row_idx++;
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    ps_gc_root_push(vm, PS_GC_ROOT_OBJECT, buf_obj);
    PSValue out = img_make_image_object(vm, width, height, buf_obj, expected);
    ps_gc_root_pop(vm, 1);
    if (img_timing_enabled()) {
        uint64_t dt = img_now_ms() - t0;
        fprintf(stderr,
                "img_timing decodeJPEG %dx%d input=%zuB output=%zuB %llums\n",
                width,
                height,
                buf->size,
                expected,
                (unsigned long long)dt);
    }
    return out;
}

static PSValue ps_native_img_encode_png(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (argc < 1) {
        img_throw(vm, "ArgumentError", "Image.encodePNG expects (image)");
        return ps_value_undefined();
    }
    int width = 0;
    int height = 0;
    PSBuffer *src_buf = NULL;
    if (!img_extract_image(vm, argv[0], &width, &height, &src_buf)) return ps_value_undefined();
    uint64_t t0 = 0;
    if (img_timing_enabled()) t0 = img_now_ms();

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        img_throw(vm, "ResourceLimitError", "PNG encoder init failed");
        return ps_value_undefined();
    }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, NULL);
        img_throw(vm, "ResourceLimitError", "PNG encoder init failed");
        return ps_value_undefined();
    }
    PSPngWriteState state = {0};
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        free(state.data);
        img_throw(vm, "EncodeError", "PNG encoding failed");
        return ps_value_undefined();
    }

    png_set_write_fn(png, &state, img_png_write, img_png_flush);
    png_set_IHDR(png, info,
                 (png_uint_32)width,
                 (png_uint_32)height,
                 8,
                 PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE,
                 PNG_FILTER_TYPE_BASE);
    png_write_info(png, info);

    png_bytep *rows = (png_bytep *)malloc(sizeof(png_bytep) * (size_t)height);
    if (!rows) {
        png_destroy_write_struct(&png, &info);
        free(state.data);
        img_throw(vm, "ResourceLimitError", "Out of memory");
        return ps_value_undefined();
    }
    size_t row_bytes = (size_t)width * 4;
    for (int y = 0; y < height; y++) {
        rows[y] = (png_bytep)(src_buf->data + (size_t)y * row_bytes);
    }
    png_write_image(png, rows);
    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    free(rows);

    PSObject *buf_obj = ps_buffer_new(vm, state.size);
    if (!buf_obj) {
        free(state.data);
        img_throw(vm, "ResourceLimitError", "Out of memory");
        return ps_value_undefined();
    }
    PSBuffer *out_buf = ps_buffer_from_object(buf_obj);
    if (!out_buf) {
        free(state.data);
        img_throw(vm, "ResourceLimitError", "Out of memory");
        return ps_value_undefined();
    }
    memcpy(out_buf->data, state.data, state.size);
    free(state.data);
    if (img_timing_enabled()) {
        uint64_t dt = img_now_ms() - t0;
        fprintf(stderr,
                "img_timing encodePNG %dx%d input=%zuB output=%zuB %llums\n",
                width,
                height,
                (size_t)width * (size_t)height * 4,
                state.size,
                (unsigned long long)dt);
    }
    return ps_value_object(buf_obj);
}

static PSValue ps_native_img_encode_jpeg(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (argc < 1) {
        img_throw(vm, "ArgumentError", "Image.encodeJPEG expects (image, quality)");
        return ps_value_undefined();
    }
    int width = 0;
    int height = 0;
    PSBuffer *src_buf = NULL;
    if (!img_extract_image(vm, argv[0], &width, &height, &src_buf)) return ps_value_undefined();
    uint64_t t0 = 0;
    if (img_timing_enabled()) t0 = img_now_ms();

    int quality = 75;
    if (argc > 1) {
        double q = ps_to_number(vm, argv[1]);
        if (vm && vm->has_pending_throw) return ps_value_undefined();
        if (!isfinite(q)) {
            img_throw(vm, "ArgumentError", "Invalid JPEG quality");
            return ps_value_undefined();
        }
        if (q < 0) q = 0;
        if (q > 100) q = 100;
        quality = (int)q;
    }

    struct jpeg_compress_struct cinfo;
    PSJpegError jerr;
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = img_jpeg_error_exit;
    if (setjmp(jerr.setjmp_buffer)) {
        jpeg_destroy_compress(&cinfo);
        img_throw(vm, "EncodeError", "JPEG encoding failed");
        return ps_value_undefined();
    }
    jpeg_create_compress(&cinfo);

    unsigned char *out_buf = NULL;
    unsigned long out_size = 0;
    jpeg_mem_dest(&cinfo, &out_buf, &out_size);

    cinfo.image_width = (JDIMENSION)width;
    cinfo.image_height = (JDIMENSION)height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    JSAMPROW row[1];
    uint8_t *row_buf = (uint8_t *)malloc((size_t)width * 3);
    if (!row_buf) {
        jpeg_destroy_compress(&cinfo);
        img_throw(vm, "ResourceLimitError", "Out of memory");
        return ps_value_undefined();
    }

    while (cinfo.next_scanline < cinfo.image_height) {
        size_t y = cinfo.next_scanline;
        const uint8_t *src = src_buf->data + y * (size_t)width * 4;
        for (int x = 0; x < width; x++) {
            row_buf[x * 3 + 0] = src[x * 4 + 0];
            row_buf[x * 3 + 1] = src[x * 4 + 1];
            row_buf[x * 3 + 2] = src[x * 4 + 2];
        }
        row[0] = row_buf;
        jpeg_write_scanlines(&cinfo, row, 1);
    }

    free(row_buf);
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    PSObject *buf_obj = ps_buffer_new(vm, (size_t)out_size);
    if (!buf_obj) {
        free(out_buf);
        img_throw(vm, "ResourceLimitError", "Out of memory");
        return ps_value_undefined();
    }
    PSBuffer *dst = ps_buffer_from_object(buf_obj);
    if (!dst) {
        free(out_buf);
        img_throw(vm, "ResourceLimitError", "Out of memory");
        return ps_value_undefined();
    }
    memcpy(dst->data, out_buf, (size_t)out_size);
    free(out_buf);

    if (img_timing_enabled()) {
        uint64_t dt = img_now_ms() - t0;
        fprintf(stderr,
                "img_timing encodeJPEG %dx%d quality=%d input=%zuB output=%luB %llums\n",
                width,
                height,
                quality,
                (size_t)width * (size_t)height * 4,
                out_size,
                (unsigned long long)dt);
    }
    return ps_value_object(buf_obj);
}

static PSValue ps_native_img_resample(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (argc < 3) {
        img_throw(vm, "ArgumentError", "Image.resample expects (image, width, height, mode)");
        return ps_value_undefined();
    }
    int src_w = 0;
    int src_h = 0;
    PSBuffer *src_buf = NULL;
    if (!img_extract_image(vm, argv[0], &src_w, &src_h, &src_buf)) return ps_value_undefined();

    int dst_w = 0;
    int dst_h = 0;
    if (!img_parse_dim(vm, argv[1], &dst_w) || !img_parse_dim(vm, argv[2], &dst_h)) {
        return ps_value_undefined();
    }
    if (!img_check_limits(vm, dst_w, dst_h)) return ps_value_undefined();

    PsImgResampleInterpolation interp = PS_IMG_RESAMPLE_INTERP_CUBIC;
    if (!img_parse_mode(vm, (argc > 3) ? argv[3] : ps_value_undefined(), &interp)) {
        return ps_value_undefined();
    }

    size_t expected = 0;
    if (!img_compute_byte_len(vm, dst_w, dst_h, &expected)) return ps_value_undefined();

    uint8_t *resampled = ps_img_resample_rgba8(src_buf->data,
                                               src_w,
                                               src_h,
                                               dst_w,
                                               dst_h,
                                               interp);
    if (!resampled) {
        img_throw(vm, "ResourceLimitError", "Resample failed");
        return ps_value_undefined();
    }

    PSObject *buf_obj = ps_buffer_new(vm, expected);
    if (!buf_obj) {
        free(resampled);
        img_throw(vm, "ResourceLimitError", "Out of memory");
        return ps_value_undefined();
    }
    PSBuffer *out_buf = ps_buffer_from_object(buf_obj);
    if (!out_buf) {
        free(resampled);
        img_throw(vm, "ResourceLimitError", "Out of memory");
        return ps_value_undefined();
    }
    free(out_buf->data);
    out_buf->data = resampled;

    ps_gc_root_push(vm, PS_GC_ROOT_OBJECT, buf_obj);
    PSValue out = img_make_image_object(vm, dst_w, dst_h, buf_obj, expected);
    ps_gc_root_pop(vm, 1);
    return out;
}

void ps_img_init(PSVM *vm) {
    if (!vm || !vm->global) return;

    PSObject *img = ps_object_new(NULL);
    if (!img) return;

    PSObject *detect_fn = ps_function_new_native(ps_native_img_detect_format);
    PSObject *decode_png_fn = ps_function_new_native(ps_native_img_decode_png);
    PSObject *decode_jpeg_fn = ps_function_new_native(ps_native_img_decode_jpeg);
    PSObject *encode_png_fn = ps_function_new_native(ps_native_img_encode_png);
    PSObject *encode_jpeg_fn = ps_function_new_native(ps_native_img_encode_jpeg);
    PSObject *resample_fn = ps_function_new_native(ps_native_img_resample);

    if (detect_fn) ps_function_setup(detect_fn, vm->function_proto, vm->object_proto, NULL);
    if (decode_png_fn) ps_function_setup(decode_png_fn, vm->function_proto, vm->object_proto, NULL);
    if (decode_jpeg_fn) ps_function_setup(decode_jpeg_fn, vm->function_proto, vm->object_proto, NULL);
    if (encode_png_fn) ps_function_setup(encode_png_fn, vm->function_proto, vm->object_proto, NULL);
    if (encode_jpeg_fn) ps_function_setup(encode_jpeg_fn, vm->function_proto, vm->object_proto, NULL);
    if (resample_fn) ps_function_setup(resample_fn, vm->function_proto, vm->object_proto, NULL);

    if (detect_fn) ps_object_define(img, ps_string_from_cstr("detectFormat"), ps_value_object(detect_fn), PS_ATTR_NONE);
    if (decode_png_fn) ps_object_define(img, ps_string_from_cstr("decodePNG"), ps_value_object(decode_png_fn), PS_ATTR_NONE);
    if (decode_jpeg_fn) ps_object_define(img, ps_string_from_cstr("decodeJPEG"), ps_value_object(decode_jpeg_fn), PS_ATTR_NONE);
    if (encode_png_fn) ps_object_define(img, ps_string_from_cstr("encodePNG"), ps_value_object(encode_png_fn), PS_ATTR_NONE);
    if (encode_jpeg_fn) ps_object_define(img, ps_string_from_cstr("encodeJPEG"), ps_value_object(encode_jpeg_fn), PS_ATTR_NONE);
    if (resample_fn) ps_object_define(img, ps_string_from_cstr("resample"), ps_value_object(resample_fn), PS_ATTR_NONE);

    ps_object_define(vm->global, ps_string_from_cstr("Image"), ps_value_object(img), PS_ATTR_NONE);
}

#endif /* PS_ENABLE_MODULE_IMG */
