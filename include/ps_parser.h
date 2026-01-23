#ifndef PS_PARSER_H
#define PS_PARSER_H

#include "ps_ast.h"

PSAstNode *ps_parse(const char *source);
PSAstNode *ps_parse_with_path(const char *source, const char *path);

#endif /* PS_PARSER_H */
