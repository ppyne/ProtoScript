#include "ps_ast.h"
#include "ps_eval.h"
#include "ps_vm.h"
#include "ps_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path, size_t *out_len) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);

    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }

    size_t read = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    buf[read] = '\0';
    if (out_len) *out_len = read;
    return buf;
}

static char *read_stream(FILE *fp, size_t *out_len) {
    size_t cap = 4096;
    size_t len = 0;
    char *buf = (char *)malloc(cap + 1);
    if (!buf) return NULL;

    for (;;) {
        size_t space = cap - len;
        size_t n = fread(buf + len, 1, space, fp);
        len += n;
        if (n == 0) {
            if (ferror(fp)) {
                free(buf);
                return NULL;
            }
            break;
        }
        if (len == cap) {
            size_t new_cap = cap * 2;
            char *next = (char *)realloc(buf, new_cap + 1);
            if (!next) {
                free(buf);
                return NULL;
            }
            buf = next;
            cap = new_cap;
        }
    }

    buf[len] = '\0';
    if (out_len) *out_len = len;
    return buf;
}

int main(int argc, char **argv) {
    size_t source_len = 0;
    char *source = NULL;
    if (argc < 2 || strcmp(argv[1], "-") == 0) {
        source = read_stream(stdin, &source_len);
    } else {
        source = read_file(argv[1], &source_len);
    }
    if (!source) {
        if (argc < 2 || strcmp(argv[1], "-") == 0) {
            fprintf(stderr, "Could not read from stdin\n");
        } else {
            fprintf(stderr, "Could not read file: %s\n", argv[1]);
        }
        return 1;
    }

    PSVM *vm = ps_vm_new();
    if (!vm) {
        fprintf(stderr, "Failed to initialize VM\n");
        free(source);
        return 1;
    }

    PSAstNode *program = ps_parse(source);
    if (!program) {
        ps_vm_free(vm);
        free(source);
        return 1;
    }
    ps_eval(vm, program);
    ps_ast_free(program);
    ps_vm_free(vm);
    free(source);
    return 0;
}
