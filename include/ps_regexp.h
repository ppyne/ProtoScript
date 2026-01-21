#ifndef PS_REGEXP_H
#define PS_REGEXP_H

#include "ps_string.h"

struct PSRegexNode;

typedef struct PSRegex {
    PSString *source;
    struct PSRegexNode *ast;
    int capture_count;
    int global;
    int ignore_case;
} PSRegex;

void ps_regex_free(PSRegex *re);

#endif /* PS_REGEXP_H */
