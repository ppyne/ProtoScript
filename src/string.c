#include "ps_string.h"
#include "ps_gc.h"
#include "ps_config.h"
#include "ps_vm.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

/* --------------------------------------------------------- */
/* UTF-8 helpers                                             */
/* --------------------------------------------------------- */

static int utf8_glyph_len(unsigned char c) {
    if ((c & 0x80) == 0x00) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return -1;
}

static uint32_t utf8_decode(const unsigned char *p, int len) {
    switch (len) {
        case 1: return p[0];
        case 2: return ((p[0] & 0x1F) << 6)  | (p[1] & 0x3F);
        case 3: return ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        case 4: return ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12)
                              | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        default: return 0;
    }
}

/* --------------------------------------------------------- */
/* String creation                                           */
/* --------------------------------------------------------- */

PSString *ps_string_from_utf8(const char *data, size_t byte_len) {
    PSString *s = (PSString *)ps_gc_alloc(PS_GC_STRING, sizeof(PSString));
    if (!s) return NULL;
#if PS_ENABLE_PERF
    {
        PSVM *vm = ps_gc_active_vm();
        if (vm) {
            vm->perf.string_new++;
        }
    }
#endif

    s->utf8 = malloc(byte_len);
    if (!s->utf8) {
        ps_string_free(s);
        return NULL;
    }

    memcpy(s->utf8, data, byte_len);
    s->byte_len = byte_len;
    /* FNV-1a hash on bytes */
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < byte_len; i++) {
        hash ^= (uint8_t)s->utf8[i];
        hash *= 16777619u;
    }
    s->hash = hash;
    s->index_state = 0;
    s->index_value = 0;

    /* First pass: count glyphs */
    size_t i = 0;
    size_t count = 0;
    int ascii_only = 1;
    while (i < byte_len) {
        unsigned char c = (unsigned char)s->utf8[i];
        if (c & 0x80) ascii_only = 0;
        int len = utf8_glyph_len(c);
        if (len <= 0 || i + len > byte_len) {
            /* invalid UTF-8 */
            ps_string_free(s);
            return NULL;
        }
        i += len;
        count++;
    }

    if (ascii_only) {
        s->glyph_count = byte_len;
        s->glyph_offsets = NULL;
        return s;
    }

    s->glyph_count = count;
    if (count == 0) {
        s->glyph_offsets = NULL;
        return s;
    }

    s->glyph_offsets = malloc(sizeof(uint32_t) * count);
    if (!s->glyph_offsets) {
        ps_string_free(s);
        return NULL;
    }

    /* Second pass: record offsets */
    i = 0;
    size_t g = 0;
    while (i < byte_len) {
        s->glyph_offsets[g++] = (uint32_t)i;
        i += utf8_glyph_len((unsigned char)s->utf8[i]);
    }

    return s;
}

PSString *ps_string_from_cstr(const char *cstr) {
#if PS_ENABLE_PERF
    {
        PSVM *vm = ps_gc_active_vm();
        if (vm) {
            vm->perf.string_from_cstr++;
        }
    }
#endif
    if (!cstr) return ps_string_from_utf8("", 0);
    size_t len = strlen(cstr);
    PSVM *vm = ps_gc_active_vm();
    if (vm && vm->intern_cache && vm->intern_cache_size > 0 && len <= 64) {
        uint32_t hash = 2166136261u;
        for (size_t i = 0; i < len; i++) {
            hash ^= (uint8_t)cstr[i];
            hash *= 16777619u;
        }
        size_t idx = hash & (vm->intern_cache_size - 1);
        PSString *cached = vm->intern_cache[idx];
        if (cached &&
            cached->hash == hash &&
            cached->byte_len == len &&
            cached->utf8 &&
            memcmp(cached->utf8, cstr, len) == 0) {
            return cached;
        }
        PSString *s = ps_string_from_utf8(cstr, len);
        if (s) {
            vm->intern_cache[idx] = s;
        }
        return s;
    }
    return ps_string_from_utf8(cstr, len);
}

/* --------------------------------------------------------- */
/* Accessors                                                 */
/* --------------------------------------------------------- */

size_t ps_string_length(const PSString *s) {
    return s ? s->glyph_count : 0;
}

PSString *ps_string_char_at(const PSString *s, size_t index) {
    if (!s || index >= s->glyph_count) {
        return ps_string_from_cstr("");
    }

    if (!s->glyph_offsets) {
        return ps_string_from_utf8(s->utf8 + index, 1);
    }

    size_t start = s->glyph_offsets[index];
    size_t end = (index + 1 < s->glyph_count)
        ? s->glyph_offsets[index + 1]
        : s->byte_len;

    return ps_string_from_utf8(s->utf8 + start, end - start);
}

uint32_t ps_string_char_code_at(const PSString *s, size_t index) {
    if (!s || index >= s->glyph_count) {
        return 0; /* NaN handled at PSValue level */
    }

    if (!s->glyph_offsets) {
        return (uint8_t)s->utf8[index];
    }

    size_t offset = s->glyph_offsets[index];
    int len = utf8_glyph_len((unsigned char)s->utf8[offset]);

    return utf8_decode((unsigned char *)s->utf8 + offset, len);
}

/* --------------------------------------------------------- */
/* Operations                                                */
/* --------------------------------------------------------- */

PSString *ps_string_concat(const PSString *a, const PSString *b) {
    if (!a) return (PSString *)b;
    if (!b) return (PSString *)a;

    size_t len = a->byte_len + b->byte_len;
    char *buf = malloc(len);
    if (!buf) return NULL;

    memcpy(buf, a->utf8, a->byte_len);
    memcpy(buf + a->byte_len, b->utf8, b->byte_len);

    PSString *res = ps_string_from_utf8(buf, len);
    free(buf);
    return res;
}

static int ps_is_ascii_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static int ps_hex_value(unsigned char c) {
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'a' && c <= 'f') return (int)(c - 'a') + 10;
    if (c >= 'A' && c <= 'F') return (int)(c - 'A') + 10;
    return -1;
}

double ps_string_to_number(const PSString *s) {
    if (!s || s->byte_len == 0) return 0.0;

    size_t start = 0;
    size_t end = s->byte_len;
    while (start < end && ps_is_ascii_space((unsigned char)s->utf8[start])) start++;
    while (end > start && ps_is_ascii_space((unsigned char)s->utf8[end - 1])) end--;
    if (start == end) return 0.0;

    size_t len = end - start;
    char *tmp = malloc(len + 1);
    if (!tmp) return 0.0;
    memcpy(tmp, s->utf8 + start, len);
    tmp[len] = '\0';

    const char *p = tmp;
    int sign = 1;
    if (*p == '+' || *p == '-') {
        if (*p == '-') sign = -1;
        p++;
    }
    if (strcmp(p, "Infinity") == 0) {
        free(tmp);
        return sign > 0 ? INFINITY : -INFINITY;
    }
    if (strcmp(p, "NaN") == 0) {
        free(tmp);
        return NAN;
    }
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        double value = 0.0;
        int digits = 0;
        for (size_t i = 2; p[i]; i++) {
            int v = ps_hex_value((unsigned char)p[i]);
            if (v < 0) {
                free(tmp);
                return NAN;
            }
            value = value * 16.0 + (double)v;
            digits++;
        }
        free(tmp);
        if (!digits) return NAN;
        return sign > 0 ? value : -value;
    }

    const char *q = p;
    int saw_digit = 0;
    while (isdigit((unsigned char)*q)) {
        saw_digit = 1;
        q++;
    }
    if (*q == '.') {
        q++;
        while (isdigit((unsigned char)*q)) {
            saw_digit = 1;
            q++;
        }
    }
    if (!saw_digit) {
        free(tmp);
        return NAN;
    }
    if (*q == 'e' || *q == 'E') {
        q++;
        if (*q == '+' || *q == '-') q++;
        const char *exp_start = q;
        while (isdigit((unsigned char)*q)) q++;
        if (q == exp_start) {
            free(tmp);
            return NAN;
        }
    }
    if (*q != '\0') {
        free(tmp);
        return NAN;
    }

    char *endptr = NULL;
    double val = strtod(tmp, &endptr);
    if (!endptr || *endptr != '\0') {
        free(tmp);
        return NAN;
    }
    free(tmp);
    return val;
}

/* --------------------------------------------------------- */
/* Debug / cleanup                                           */
/* --------------------------------------------------------- */

void ps_string_debug_dump(const PSString *s) {
    if (!s) {
        printf("<String NULL>\n");
        return;
    }

    printf("String(bytes=%zu, glyphs=%zu): \"", s->byte_len, s->glyph_count);
    fwrite(s->utf8, 1, s->byte_len, stdout);
    printf("\"\n");
}

void ps_string_free(PSString *s) {
    if (!s) return;
    if (ps_gc_is_managed(s)) {
        ps_gc_free(s);
        return;
    }
    free(s->utf8);
    free(s->glyph_offsets);
    free(s);
}
