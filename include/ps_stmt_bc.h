#ifndef PS_STMT_BC_H
#define PS_STMT_BC_H

#include <stddef.h>
#include <stdint.h>
#include "ps_value.h"

typedef struct PSStmtBCInstr {
    uint8_t op;
    uint8_t flags;
    uint16_t pad;
    int32_t a;
    void *ptr;
    void *ptr2;
} PSStmtBCInstr;

typedef struct PSStmtBC {
    PSStmtBCInstr *code;
    size_t count;
    PSValue *consts;
    size_t const_count;
} PSStmtBC;

void ps_stmt_bc_free(PSStmtBC *bc);

#endif /* PS_STMT_BC_H */
