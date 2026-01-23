#ifndef PS_IMG_H
#define PS_IMG_H

#include <stddef.h>

struct PSVM;

typedef struct PSImageHandle {
    size_t byte_len;
} PSImageHandle;

PSImageHandle *ps_img_handle_new(struct PSVM *vm, size_t byte_len);
void ps_img_handle_release(PSImageHandle *handle);

#endif /* PS_IMG_H */
