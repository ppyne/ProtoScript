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

PSObject *ps_buffer_new(struct PSVM *vm, size_t size);
PSBuffer *ps_buffer_from_object(PSObject *obj);
void ps_buffer_init(struct PSVM *vm);

#endif /* PS_BUFFER_H */
