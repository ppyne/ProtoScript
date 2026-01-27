#include "ps_expr_bc.h"

#include <stdlib.h>

void ps_expr_bc_free(PSExprBC *bc) {
    if (!bc) return;
    free(bc->code);
    free(bc->consts);
    free(bc);
}
