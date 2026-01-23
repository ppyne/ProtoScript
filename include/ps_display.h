#ifndef PS_DISPLAY_H
#define PS_DISPLAY_H

#include "ps_vm.h"

typedef struct PSDisplay {
    int is_open;
    int was_open;
    int logical_width;
    int logical_height;
    int window_width;
    int window_height;
    int last_logical_width;
    int last_logical_height;
    int resizable;
    int scale_mode;
    struct SDL_Window *window;
    struct SDL_Renderer *renderer;
    struct SDL_Texture *texture;
    struct PSObject *framebuffer_obj;
} PSDisplay;

void ps_display_init(PSVM *vm);
void ps_display_shutdown(PSVM *vm);
void ps_display_poll_events(PSVM *vm);

#endif /* PS_DISPLAY_H */
