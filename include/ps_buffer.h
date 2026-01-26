#ifndef PS_BUFFER_H
#define PS_BUFFER_H

#include <stddef.h>
#include <stdint.h>

#include "ps_object.h"

struct PSVM;

typedef struct PSBuffer {
    size_t size;
    uint8_t *data;
} PSBuffer;

typedef struct PSBuffer32 {
    PSObject *source;
    size_t offset;
    size_t length;
} PSBuffer32;

PSObject *ps_buffer_new(struct PSVM *vm, size_t size);
PSBuffer *ps_buffer_from_object(PSObject *obj);
PSObject *ps_buffer32_new(struct PSVM *vm, size_t length);
PSObject *ps_buffer32_view(struct PSVM *vm, PSObject *buffer_obj, size_t offset, size_t length);
PSBuffer32 *ps_buffer32_from_object(PSObject *obj);
void ps_buffer_init(struct PSVM *vm);

#endif /* PS_BUFFER_H */
