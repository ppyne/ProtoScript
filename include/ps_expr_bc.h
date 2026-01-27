#ifndef PS_EXPR_BC_H
#define PS_EXPR_BC_H

#include <stddef.h>
#include <stdint.h>
#include "ps_value.h"

typedef struct PSExprBCInstr {
    uint8_t op;
    uint8_t flags;
    uint16_t pad;
    int32_t a;
    void *ptr;
} PSExprBCInstr;

typedef struct PSExprBC {
    PSExprBCInstr *code;
    size_t count;
    PSValue *consts;
    size_t const_count;
} PSExprBC;

void ps_expr_bc_free(PSExprBC *bc);

#endif /* PS_EXPR_BC_H */
