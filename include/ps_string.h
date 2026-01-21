#ifndef PS_STRING_H
#define PS_STRING_H

#include <stddef.h>
#include <stdint.h>

/* String object */
typedef struct PSString {
    char     *utf8;          /* UTF-8 buffer (not null-terminated) */
    size_t    byte_len;      /* length in bytes */
    uint32_t *glyph_offsets; /* byte offsets for each glyph */
    size_t    glyph_count;   /* number of glyphs */
} PSString;

/* Creation */
PSString *ps_string_from_utf8(const char *data, size_t byte_len);
PSString *ps_string_from_cstr(const char *cstr);

/* Accessors */
size_t    ps_string_length(const PSString *s);
PSString *ps_string_char_at(const PSString *s, size_t index);
uint32_t  ps_string_char_code_at(const PSString *s, size_t index);

/* Utilities */
PSString *ps_string_concat(const PSString *a, const PSString *b);
double    ps_string_to_number(const PSString *s);

/* Debug */
void ps_string_debug_dump(const PSString *s);

/* Destruction (used by GC later) */
void ps_string_free(PSString *s);

#endif /* PS_STRING_H */
