#include "ps_display.h"
#include "ps_config.h"
#include "ps_buffer.h"
#include "ps_event.h"
#include "ps_eval.h"
#include "ps_function.h"
#include "ps_object.h"
#include "ps_string.h"
#include "ps_value.h"

#if PS_ENABLE_SDL
#include <SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum {
    PS_DISPLAY_SCALE_NONE = 0,
    PS_DISPLAY_SCALE_CENTERED = 1,
    PS_DISPLAY_SCALE_FIT = 2,
    PS_DISPLAY_SCALE_STRETCH = 3
};

static int display_sdl_ready = 0;

static void display_throw(PSVM *vm, const char *name, const char *message) {
    if (!vm) return;
    vm->pending_throw = ps_vm_make_error(vm, name ? name : "Error", message ? message : "");
    vm->has_pending_throw = 1;
}

static PSDisplay *display_state(PSVM *vm) {
    return vm ? vm->display : NULL;
}

static int display_require_open(PSVM *vm) {
    PSDisplay *d = display_state(vm);
    if (!d || !d->is_open) {
        if (!d || !d->was_open) {
            display_throw(vm, "Error", "Display not open");
        }
        return 0;
    }
    return 1;
}

static uint8_t display_clamp_color(double v) {
    if (isnan(v) || isinf(v)) return 0;
    if (v <= 0.0) return 0;
    if (v >= 255.0) return 255;
    return (uint8_t)v;
}

static int display_parse_size(PSVM *vm, PSValue value, int *out) {
    double num = ps_to_number(vm, value);
    if (vm && vm->has_pending_throw) return 0;
    if (isnan(num) || isinf(num) || num <= 0.0 || floor(num) != num) {
        display_throw(vm, "RangeError", "Invalid display size");
        return 0;
    }
    if (out) *out = (int)num;
    return 1;
}

static PSBuffer *display_buffer(PSDisplay *d) {
    if (!d || !d->framebuffer_obj) return NULL;
    return ps_buffer_from_object(d->framebuffer_obj);
}

static void display_invalidate_buffer(PSDisplay *d) {
    if (!d || !d->framebuffer_obj) return;
    PSBuffer *buf = ps_buffer_from_object(d->framebuffer_obj);
    if (!buf) return;
    free(buf->data);
    buf->data = NULL;
    buf->size = 0;
}

static int display_recreate_framebuffer(PSVM *vm, PSDisplay *d, int width, int height) {
    if (!d) return 0;
    display_invalidate_buffer(d);
    PSObject *buf_obj = ps_buffer_new(vm, (size_t)width * (size_t)height * 4u);
    if (!buf_obj) return 0;
    d->framebuffer_obj = buf_obj;
    return 1;
}

static int display_recreate_texture(PSDisplay *d) {
    if (!d || !d->renderer) return 0;
    if (d->texture) {
        SDL_DestroyTexture(d->texture);
        d->texture = NULL;
    }
    d->texture = SDL_CreateTexture(d->renderer,
                                   SDL_PIXELFORMAT_RGBA8888,
                                   SDL_TEXTUREACCESS_STREAMING,
                                   d->logical_width,
                                   d->logical_height);
    return d->texture != NULL;
}

static void display_plot(PSDisplay *d, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (!d) return;
    if (x < 0 || y < 0 || x >= d->logical_width || y >= d->logical_height) return;
    PSBuffer *buf = display_buffer(d);
    if (!buf || !buf->data) return;
    size_t idx = ((size_t)y * (size_t)d->logical_width + (size_t)x) * 4u;
    if (idx + 3 >= buf->size) return;
    buf->data[idx + 0] = r;
    buf->data[idx + 1] = g;
    buf->data[idx + 2] = b;
    buf->data[idx + 3] = 255;
}

static void display_bresenham(PSDisplay *d,
                              int x0, int y0,
                              int x1, int y1,
                              uint8_t r, uint8_t g, uint8_t b) {
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        display_plot(d, x0, y0, r, g, b);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void display_push_event(PSVM *vm, const char *type) {
    ps_event_push(vm, type);
}

static void display_push_key_event(PSVM *vm, const char *type, const SDL_KeyboardEvent *ev) {
    if (!vm || !ev) return;
    PSObject *obj = ps_object_new(vm->object_proto);
    if (!obj) return;
    const char *name = SDL_GetKeyName(ev->keysym.sym);
    ps_object_define(obj,
                     ps_string_from_cstr("type"),
                     ps_value_string(ps_string_from_cstr(type ? type : "")),
                     PS_ATTR_NONE);
    ps_object_define(obj,
                     ps_string_from_cstr("key"),
                     ps_value_string(ps_string_from_cstr(name ? name : "")),
                     PS_ATTR_NONE);
    ps_object_define(obj,
                     ps_string_from_cstr("code"),
                     ps_value_number((double)ev->keysym.scancode),
                     PS_ATTR_NONE);
    if (strcmp(type, "keydown") == 0) {
        ps_object_define(obj,
                         ps_string_from_cstr("repeat"),
                         ps_value_boolean(ev->repeat ? 1 : 0),
                         PS_ATTR_NONE);
    }
    ps_event_push_value(vm, ps_value_object(obj));
}

static void display_push_motion_event(PSVM *vm, const SDL_MouseMotionEvent *ev) {
    if (!vm || !ev) return;
    PSObject *obj = ps_object_new(vm->object_proto);
    if (!obj) return;
    ps_object_define(obj,
                     ps_string_from_cstr("type"),
                     ps_value_string(ps_string_from_cstr("mousemotion")),
                     PS_ATTR_NONE);
    ps_object_define(obj,
                     ps_string_from_cstr("x"),
                     ps_value_number((double)ev->x),
                     PS_ATTR_NONE);
    ps_object_define(obj,
                     ps_string_from_cstr("y"),
                     ps_value_number((double)ev->y),
                     PS_ATTR_NONE);
    ps_object_define(obj,
                     ps_string_from_cstr("dx"),
                     ps_value_number((double)ev->xrel),
                     PS_ATTR_NONE);
    ps_object_define(obj,
                     ps_string_from_cstr("dy"),
                     ps_value_number((double)ev->yrel),
                     PS_ATTR_NONE);
    ps_event_push_value(vm, ps_value_object(obj));
}

static void display_push_button_event(PSVM *vm, const char *type, const SDL_MouseButtonEvent *ev) {
    if (!vm || !ev) return;
    PSObject *obj = ps_object_new(vm->object_proto);
    if (!obj) return;
    ps_object_define(obj,
                     ps_string_from_cstr("type"),
                     ps_value_string(ps_string_from_cstr(type ? type : "")),
                     PS_ATTR_NONE);
    ps_object_define(obj,
                     ps_string_from_cstr("button"),
                     ps_value_number((double)ev->button),
                     PS_ATTR_NONE);
    ps_object_define(obj,
                     ps_string_from_cstr("x"),
                     ps_value_number((double)ev->x),
                     PS_ATTR_NONE);
    ps_object_define(obj,
                     ps_string_from_cstr("y"),
                     ps_value_number((double)ev->y),
                     PS_ATTR_NONE);
    ps_event_push_value(vm, ps_value_object(obj));
}

static void display_push_wheel_event(PSVM *vm, const SDL_MouseWheelEvent *ev) {
    if (!vm || !ev) return;
    PSObject *obj = ps_object_new(vm->object_proto);
    if (!obj) return;
    ps_object_define(obj,
                     ps_string_from_cstr("type"),
                     ps_value_string(ps_string_from_cstr("mousewheel")),
                     PS_ATTR_NONE);
    ps_object_define(obj,
                     ps_string_from_cstr("dx"),
                     ps_value_number((double)ev->x),
                     PS_ATTR_NONE);
    ps_object_define(obj,
                     ps_string_from_cstr("dy"),
                     ps_value_number((double)ev->y),
                     PS_ATTR_NONE);
    ps_event_push_value(vm, ps_value_object(obj));
}

static void display_push_resize_event(PSVM *vm, int width, int height) {
    if (!vm) return;
    PSObject *obj = ps_object_new(vm->object_proto);
    if (!obj) return;
    ps_object_define(obj,
                     ps_string_from_cstr("type"),
                     ps_value_string(ps_string_from_cstr("window_resized")),
                     PS_ATTR_NONE);
    ps_object_define(obj,
                     ps_string_from_cstr("width"),
                     ps_value_number((double)width),
                     PS_ATTR_NONE);
    ps_object_define(obj,
                     ps_string_from_cstr("height"),
                     ps_value_number((double)height),
                     PS_ATTR_NONE);
    ps_event_push_value(vm, ps_value_object(obj));
}

void ps_display_poll_events(PSVM *vm) {
    PSDisplay *d = display_state(vm);
    if (!d || !d->is_open) return;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
            case SDL_QUIT:
                display_push_event(vm, "quit");
                break;
            case SDL_WINDOWEVENT:
                if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    int w = ev.window.data1;
                    int h = ev.window.data2;
                    if (w > 0 && h > 0) {
                        d->window_width = w;
                        d->window_height = h;
                        display_push_resize_event(vm, w, h);
                    }
                }
                break;
            case SDL_KEYDOWN:
                display_push_key_event(vm, "keydown", &ev.key);
                break;
            case SDL_KEYUP:
                display_push_key_event(vm, "keyup", &ev.key);
                break;
            case SDL_MOUSEMOTION:
                display_push_motion_event(vm, &ev.motion);
                break;
            case SDL_MOUSEBUTTONDOWN:
                display_push_button_event(vm, "mousebuttondown", &ev.button);
                break;
            case SDL_MOUSEBUTTONUP:
                display_push_button_event(vm, "mousebuttonup", &ev.button);
                break;
            case SDL_MOUSEWHEEL:
                display_push_wheel_event(vm, &ev.wheel);
                break;
            default:
                break;
        }
    }
}

static PSValue ps_native_display_open(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    PSDisplay *d = display_state(vm);
    if (!d) return ps_value_undefined();
    if (d->is_open) {
        display_throw(vm, "Error", "Display already open");
        return ps_value_undefined();
    }
    if (argc < 3) {
        ps_vm_throw_type_error(vm, "Display.open expects (width, height, title, options)");
        return ps_value_undefined();
    }
    int width = 0;
    int height = 0;
    if (!display_parse_size(vm, argv[0], &width)) return ps_value_undefined();
    if (!display_parse_size(vm, argv[1], &height)) return ps_value_undefined();
    PSString *title_s = ps_to_string(vm, argv[2]);
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    int resizable = 0;
    int scale_mode = PS_DISPLAY_SCALE_NONE;
    if (argc >= 4) {
        PSValue options_val = argv[3];
        if (options_val.type != PS_T_UNDEFINED && options_val.type != PS_T_NULL) {
            if (options_val.type != PS_T_OBJECT || !options_val.as.object) {
                ps_vm_throw_type_error(vm, "Display.open expects (width, height, title, options)");
                return ps_value_undefined();
            }
            PSObject *options = options_val.as.object;
            int found = 0;
            PSValue resizable_val = ps_object_get(options, ps_string_from_cstr("resizable"), &found);
            if (found) {
                resizable = ps_to_boolean(vm, resizable_val);
            }
            found = 0;
            PSValue scale_val = ps_object_get(options, ps_string_from_cstr("scale"), &found);
            if (found) {
                PSString *scale_s = ps_to_string(vm, scale_val);
                if (vm && vm->has_pending_throw) return ps_value_undefined();
                if (!scale_s) {
                    display_throw(vm, "Error", "Invalid scale mode");
                    return ps_value_undefined();
                }
                if (scale_s->byte_len == 4 && memcmp(scale_s->utf8, "none", 4) == 0) {
                    scale_mode = PS_DISPLAY_SCALE_NONE;
                } else if (scale_s->byte_len == 8 && memcmp(scale_s->utf8, "centered", 8) == 0) {
                    scale_mode = PS_DISPLAY_SCALE_CENTERED;
                } else if (scale_s->byte_len == 3 && memcmp(scale_s->utf8, "fit", 3) == 0) {
                    scale_mode = PS_DISPLAY_SCALE_FIT;
                } else if (scale_s->byte_len == 7 && memcmp(scale_s->utf8, "stretch", 7) == 0) {
                    scale_mode = PS_DISPLAY_SCALE_STRETCH;
                } else {
                    display_throw(vm, "Error", "Invalid scale mode");
                    return ps_value_undefined();
                }
            }
        }
    }
    char *title = NULL;
    if (title_s) {
        title = (char *)malloc(title_s->byte_len + 1);
        if (title) {
            memcpy(title, title_s->utf8, title_s->byte_len);
            title[title_s->byte_len] = '\0';
        }
    }
    if (!display_sdl_ready) {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            free(title);
            display_throw(vm, "Error", "Unable to initialize display");
            return ps_value_undefined();
        }
        display_sdl_ready = 1;
    }

    Uint32 window_flags = SDL_WINDOW_SHOWN;
    if (resizable) window_flags |= SDL_WINDOW_RESIZABLE;
    d->window = SDL_CreateWindow(title ? title : "ProtoScript",
                                 SDL_WINDOWPOS_CENTERED,
                                 SDL_WINDOWPOS_CENTERED,
                                 width,
                                 height,
                                 window_flags);
    free(title);
    if (!d->window) {
        display_throw(vm, "Error", "Unable to create window");
        return ps_value_undefined();
    }
    d->renderer = SDL_CreateRenderer(d->window, -1, SDL_RENDERER_SOFTWARE);
    if (!d->renderer) {
        SDL_DestroyWindow(d->window);
        d->window = NULL;
        display_throw(vm, "Error", "Unable to create renderer");
        return ps_value_undefined();
    }
    d->logical_width = width;
    d->logical_height = height;
    d->window_width = width;
    d->window_height = height;
    d->resizable = resizable;
    d->scale_mode = scale_mode;
    if (!display_recreate_texture(d)) {
        SDL_DestroyRenderer(d->renderer);
        SDL_DestroyWindow(d->window);
        d->renderer = NULL;
        d->window = NULL;
        display_throw(vm, "Error", "Unable to create framebuffer");
        return ps_value_undefined();
    }
    if (!display_recreate_framebuffer(vm, d, width, height)) {
        SDL_DestroyTexture(d->texture);
        SDL_DestroyRenderer(d->renderer);
        SDL_DestroyWindow(d->window);
        d->texture = NULL;
        d->renderer = NULL;
        d->window = NULL;
        display_throw(vm, "Error", "Unable to allocate framebuffer");
        return ps_value_undefined();
    }
    d->is_open = 1;
    if (d->was_open &&
        (d->last_logical_width != width || d->last_logical_height != height)) {
        PSObject *ev = ps_object_new(vm->object_proto);
        if (ev) {
            ps_object_define(ev,
                             ps_string_from_cstr("type"),
                             ps_value_string(ps_string_from_cstr("framebuffer_changed")),
                             PS_ATTR_NONE);
            ps_object_define(ev,
                             ps_string_from_cstr("width"),
                             ps_value_number((double)width),
                             PS_ATTR_NONE);
            ps_object_define(ev,
                             ps_string_from_cstr("height"),
                             ps_value_number((double)height),
                             PS_ATTR_NONE);
            ps_event_push_value(vm, ps_value_object(ev));
        }
    }
    d->was_open = 1;
    return ps_value_undefined();
}

static PSValue ps_native_display_close(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    PSDisplay *d = display_state(vm);
    if (!d || !d->is_open) return ps_value_undefined();
    display_invalidate_buffer(d);
    d->framebuffer_obj = NULL;
    if (d->texture) SDL_DestroyTexture(d->texture);
    if (d->renderer) SDL_DestroyRenderer(d->renderer);
    if (d->window) SDL_DestroyWindow(d->window);
    d->texture = NULL;
    d->renderer = NULL;
    d->window = NULL;
    d->is_open = 0;
    d->was_open = 1;
    d->last_logical_width = d->logical_width;
    d->last_logical_height = d->logical_height;
    return ps_value_undefined();
}

static PSValue ps_native_display_size(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    if (!display_require_open(vm)) return ps_value_undefined();
    PSDisplay *d = display_state(vm);
    PSObject *obj = ps_object_new(vm->object_proto);
    if (!obj) return ps_value_undefined();
    ps_object_define(obj,
                     ps_string_from_cstr("width"),
                     ps_value_number((double)d->logical_width),
                     PS_ATTR_NONE);
    ps_object_define(obj,
                     ps_string_from_cstr("height"),
                     ps_value_number((double)d->logical_height),
                     PS_ATTR_NONE);
    return ps_value_object(obj);
}

static PSValue ps_native_display_clear(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (!display_require_open(vm)) return ps_value_undefined();
    PSDisplay *d = display_state(vm);
    if (argc < 3) {
        ps_vm_throw_type_error(vm, "Display.clear expects (r, g, b)");
        return ps_value_undefined();
    }
    uint8_t r = display_clamp_color(ps_to_number(vm, argv[0]));
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    uint8_t g = display_clamp_color(ps_to_number(vm, argv[1]));
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    uint8_t b = display_clamp_color(ps_to_number(vm, argv[2]));
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    PSBuffer *buf = display_buffer(d);
    if (!buf || !buf->data) return ps_value_undefined();
    for (size_t i = 0; i + 3 < buf->size; i += 4) {
        buf->data[i + 0] = r;
        buf->data[i + 1] = g;
        buf->data[i + 2] = b;
        buf->data[i + 3] = 255;
    }
    return ps_value_undefined();
}

static PSValue ps_native_display_pixel(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (!display_require_open(vm)) return ps_value_undefined();
    if (argc < 5) {
        ps_vm_throw_type_error(vm, "Display.pixel expects (x, y, r, g, b)");
        return ps_value_undefined();
    }
    PSDisplay *d = display_state(vm);
    int x = (int)ps_to_number(vm, argv[0]);
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    int y = (int)ps_to_number(vm, argv[1]);
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    uint8_t r = display_clamp_color(ps_to_number(vm, argv[2]));
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    uint8_t g = display_clamp_color(ps_to_number(vm, argv[3]));
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    uint8_t b = display_clamp_color(ps_to_number(vm, argv[4]));
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    display_plot(d, x, y, r, g, b);
    return ps_value_undefined();
}

static PSValue ps_native_display_line(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (!display_require_open(vm)) return ps_value_undefined();
    if (argc < 7) {
        ps_vm_throw_type_error(vm, "Display.line expects (x1, y1, x2, y2, r, g, b)");
        return ps_value_undefined();
    }
    PSDisplay *d = display_state(vm);
    int x1 = (int)ps_to_number(vm, argv[0]);
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    int y1 = (int)ps_to_number(vm, argv[1]);
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    int x2 = (int)ps_to_number(vm, argv[2]);
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    int y2 = (int)ps_to_number(vm, argv[3]);
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    uint8_t r = display_clamp_color(ps_to_number(vm, argv[4]));
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    uint8_t g = display_clamp_color(ps_to_number(vm, argv[5]));
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    uint8_t b = display_clamp_color(ps_to_number(vm, argv[6]));
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    display_bresenham(d, x1, y1, x2, y2, r, g, b);
    return ps_value_undefined();
}

static PSValue ps_native_display_rect(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (!display_require_open(vm)) return ps_value_undefined();
    if (argc < 7) {
        ps_vm_throw_type_error(vm, "Display.rect expects (x, y, w, h, r, g, b)");
        return ps_value_undefined();
    }
    PSDisplay *d = display_state(vm);
    int x = (int)ps_to_number(vm, argv[0]);
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    int y = (int)ps_to_number(vm, argv[1]);
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    int w = (int)ps_to_number(vm, argv[2]);
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    int h = (int)ps_to_number(vm, argv[3]);
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    uint8_t r = display_clamp_color(ps_to_number(vm, argv[4]));
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    uint8_t g = display_clamp_color(ps_to_number(vm, argv[5]));
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    uint8_t b = display_clamp_color(ps_to_number(vm, argv[6]));
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    if (w <= 0 || h <= 0) {
        return ps_value_undefined();
    }
    display_bresenham(d, x, y, x + w - 1, y, r, g, b);
    display_bresenham(d, x, y + h - 1, x + w - 1, y + h - 1, r, g, b);
    display_bresenham(d, x, y, x, y + h - 1, r, g, b);
    display_bresenham(d, x + w - 1, y, x + w - 1, y + h - 1, r, g, b);
    return ps_value_undefined();
}

static PSValue ps_native_display_fill_rect(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    if (!display_require_open(vm)) return ps_value_undefined();
    if (argc < 7) {
        ps_vm_throw_type_error(vm, "Display.fillRect expects (x, y, w, h, r, g, b)");
        return ps_value_undefined();
    }
    PSDisplay *d = display_state(vm);
    int x = (int)ps_to_number(vm, argv[0]);
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    int y = (int)ps_to_number(vm, argv[1]);
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    int w = (int)ps_to_number(vm, argv[2]);
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    int h = (int)ps_to_number(vm, argv[3]);
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    uint8_t r = display_clamp_color(ps_to_number(vm, argv[4]));
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    uint8_t g = display_clamp_color(ps_to_number(vm, argv[5]));
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    uint8_t b = display_clamp_color(ps_to_number(vm, argv[6]));
    if (vm && vm->has_pending_throw) return ps_value_undefined();
    if (w <= 0 || h <= 0) {
        return ps_value_undefined();
    }
    for (int yy = 0; yy < h; yy++) {
        for (int xx = 0; xx < w; xx++) {
            display_plot(d, x + xx, y + yy, r, g, b);
        }
    }
    return ps_value_undefined();
}

static PSValue ps_native_display_present(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    if (!display_require_open(vm)) return ps_value_undefined();
    PSDisplay *d = display_state(vm);
    PSBuffer *buf = display_buffer(d);
    if (!buf || !buf->data || !d->texture) return ps_value_undefined();
    SDL_UpdateTexture(d->texture, NULL, buf->data, d->logical_width * 4);
    SDL_SetRenderDrawColor(d->renderer, 0, 0, 0, 255);
    SDL_RenderClear(d->renderer);

    SDL_Rect dst;
    if (d->scale_mode == PS_DISPLAY_SCALE_STRETCH) {
        dst.x = 0;
        dst.y = 0;
        dst.w = d->window_width;
        dst.h = d->window_height;
    } else if (d->scale_mode == PS_DISPLAY_SCALE_FIT) {
        double sx = (double)d->window_width / (double)d->logical_width;
        double sy = (double)d->window_height / (double)d->logical_height;
        double s = (sx < sy) ? sx : sy;
        int w = (int)floor((double)d->logical_width * s);
        int h = (int)floor((double)d->logical_height * s);
        dst.w = w;
        dst.h = h;
        dst.x = (d->window_width - w) / 2;
        dst.y = (d->window_height - h) / 2;
    } else if (d->scale_mode == PS_DISPLAY_SCALE_CENTERED) {
        dst.w = d->logical_width;
        dst.h = d->logical_height;
        dst.x = (d->window_width - d->logical_width) / 2;
        dst.y = (d->window_height - d->logical_height) / 2;
    } else {
        dst.x = 0;
        dst.y = 0;
        dst.w = d->logical_width;
        dst.h = d->logical_height;
    }

    SDL_RenderCopy(d->renderer, d->texture, NULL, &dst);
    SDL_RenderPresent(d->renderer);
    return ps_value_undefined();
}

static PSValue ps_native_display_framebuffer(PSVM *vm, PSValue this_val, int argc, PSValue *argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    if (!display_require_open(vm)) return ps_value_undefined();
    PSDisplay *d = display_state(vm);
    return d->framebuffer_obj ? ps_value_object(d->framebuffer_obj) : ps_value_undefined();
}

void ps_display_init(PSVM *vm) {
    if (!vm || !vm->global) return;
    PSObject *display = ps_object_new(NULL);
    if (!display) return;

    PSObject *open_fn = ps_function_new_native(ps_native_display_open);
    PSObject *close_fn = ps_function_new_native(ps_native_display_close);
    PSObject *size_fn = ps_function_new_native(ps_native_display_size);
    PSObject *clear_fn = ps_function_new_native(ps_native_display_clear);
    PSObject *pixel_fn = ps_function_new_native(ps_native_display_pixel);
    PSObject *line_fn = ps_function_new_native(ps_native_display_line);
    PSObject *rect_fn = ps_function_new_native(ps_native_display_rect);
    PSObject *fill_rect_fn = ps_function_new_native(ps_native_display_fill_rect);
    PSObject *present_fn = ps_function_new_native(ps_native_display_present);
    PSObject *framebuffer_fn = ps_function_new_native(ps_native_display_framebuffer);

    if (open_fn) ps_function_setup(open_fn, vm->function_proto, vm->object_proto, NULL);
    if (close_fn) ps_function_setup(close_fn, vm->function_proto, vm->object_proto, NULL);
    if (size_fn) ps_function_setup(size_fn, vm->function_proto, vm->object_proto, NULL);
    if (clear_fn) ps_function_setup(clear_fn, vm->function_proto, vm->object_proto, NULL);
    if (pixel_fn) ps_function_setup(pixel_fn, vm->function_proto, vm->object_proto, NULL);
    if (line_fn) ps_function_setup(line_fn, vm->function_proto, vm->object_proto, NULL);
    if (rect_fn) ps_function_setup(rect_fn, vm->function_proto, vm->object_proto, NULL);
    if (fill_rect_fn) ps_function_setup(fill_rect_fn, vm->function_proto, vm->object_proto, NULL);
    if (present_fn) ps_function_setup(present_fn, vm->function_proto, vm->object_proto, NULL);
    if (framebuffer_fn) ps_function_setup(framebuffer_fn, vm->function_proto, vm->object_proto, NULL);

    if (open_fn) ps_object_define(display, ps_string_from_cstr("open"), ps_value_object(open_fn), PS_ATTR_NONE);
    if (close_fn) ps_object_define(display, ps_string_from_cstr("close"), ps_value_object(close_fn), PS_ATTR_NONE);
    if (size_fn) ps_object_define(display, ps_string_from_cstr("size"), ps_value_object(size_fn), PS_ATTR_NONE);
    if (clear_fn) ps_object_define(display, ps_string_from_cstr("clear"), ps_value_object(clear_fn), PS_ATTR_NONE);
    if (pixel_fn) ps_object_define(display, ps_string_from_cstr("pixel"), ps_value_object(pixel_fn), PS_ATTR_NONE);
    if (line_fn) ps_object_define(display, ps_string_from_cstr("line"), ps_value_object(line_fn), PS_ATTR_NONE);
    if (rect_fn) ps_object_define(display, ps_string_from_cstr("rect"), ps_value_object(rect_fn), PS_ATTR_NONE);
    if (fill_rect_fn) ps_object_define(display, ps_string_from_cstr("fillRect"),
                                       ps_value_object(fill_rect_fn), PS_ATTR_NONE);
    if (present_fn) ps_object_define(display, ps_string_from_cstr("present"),
                                     ps_value_object(present_fn), PS_ATTR_NONE);
    if (framebuffer_fn) ps_object_define(display, ps_string_from_cstr("framebuffer"),
                                         ps_value_object(framebuffer_fn), PS_ATTR_NONE);

    ps_object_define(vm->global,
                     ps_string_from_cstr("Display"),
                     ps_value_object(display),
                     PS_ATTR_NONE);
}

void ps_display_shutdown(PSVM *vm) {
    if (!vm || !vm->display) return;
    PSDisplay *d = vm->display;
    if (d->is_open) {
        display_invalidate_buffer(d);
        if (d->texture) SDL_DestroyTexture(d->texture);
        if (d->renderer) SDL_DestroyRenderer(d->renderer);
        if (d->window) SDL_DestroyWindow(d->window);
    }
    free(d);
    vm->display = NULL;
    if (display_sdl_ready) {
        SDL_Quit();
        display_sdl_ready = 0;
    }
}

#else

void ps_display_poll_events(PSVM *vm) {
    (void)vm;
}

void ps_display_init(PSVM *vm) {
    (void)vm;
}

void ps_display_shutdown(PSVM *vm) {
    (void)vm;
}

#endif
