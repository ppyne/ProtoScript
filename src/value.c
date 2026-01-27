#include "ps_value.h"
#include "ps_string.h"
#include "ps_object.h"

#include <stdio.h>
#include <math.h>

/* Constructors */

PSValue ps_value_undefined(void) {
    PSValue v;
    v.type = PS_T_UNDEFINED;
    return v;
}

PSValue ps_value_null(void) {
    PSValue v;
    v.type = PS_T_NULL;
    return v;
}

PSValue ps_value_boolean(int value) {
    PSValue v;
    v.type = PS_T_BOOLEAN;
    v.as.boolean = value ? 1 : 0;
    return v;
}

PSValue ps_value_number(double value) {
    PSValue v;
    v.type = PS_T_NUMBER;
    v.as.number = value;
    return v;
}

PSValue ps_value_string(struct PSString *value) {
    PSValue v;
    v.type = PS_T_STRING;
    v.as.string = value;
    return v;
}

PSValue ps_value_object(struct PSObject *value) {
    PSValue v;
    v.type = PS_T_OBJECT;
    v.as.object = value;
    return v;
}

/* Utilities */

int ps_value_is_primitive(const PSValue *v) {
    return v->type != PS_T_OBJECT;
}

int ps_value_is_truthy(const PSValue *v) {
    switch (v->type) {
        case PS_T_UNDEFINED:
        case PS_T_NULL:
            return 0;
        case PS_T_BOOLEAN:
            return v->as.boolean;
        case PS_T_NUMBER:
            return !isnan(v->as.number) && v->as.number != 0.0;
        case PS_T_STRING:
            return v->as.string->glyph_count > 0;
        case PS_T_OBJECT:
            return 1;
        default:
            return 0;
    }
}

/* Conversions */

struct PSString *ps_value_to_string(const PSValue *v) {
    switch (v->type) {
        case PS_T_UNDEFINED:
            return ps_string_from_cstr("undefined");
        case PS_T_NULL:
            return ps_string_from_cstr("null");
        case PS_T_BOOLEAN:
            return ps_string_from_cstr(v->as.boolean ? "true" : "false");
        case PS_T_NUMBER: {
            char buf[64];
            if (isnan(v->as.number)) {
                snprintf(buf, sizeof(buf), "NaN");
            } else if (isinf(v->as.number)) {
                snprintf(buf, sizeof(buf), "%sInfinity", v->as.number < 0 ? "-" : "");
            } else {
                double num = v->as.number;
                if (num == 0.0 && signbit(num)) {
                    num = 0.0;
                }
                snprintf(buf, sizeof(buf), "%.15g", num);
            }
            return ps_string_from_cstr(buf);
        }
        case PS_T_STRING:
            return v->as.string;
        case PS_T_OBJECT: {
            PSObject *obj = v->as.object;
            if (!obj) {
                return ps_string_from_cstr("null");
            }
            if (obj->kind == PS_OBJ_KIND_STRING && obj->internal) {
                PSValue *inner = (PSValue *)obj->internal;
                if (inner->type == PS_T_STRING) {
                    return inner->as.string;
                }
            }
            if ((obj->kind == PS_OBJ_KIND_NUMBER || obj->kind == PS_OBJ_KIND_BOOLEAN) && obj->internal) {
                PSValue *inner = (PSValue *)obj->internal;
                return ps_value_to_string(inner);
            }
            if (obj->kind == PS_OBJ_KIND_FUNCTION) {
                return ps_string_from_cstr("[object Function]");
            }
            return ps_string_from_cstr("[object Object]");
        }
        default:
            return ps_string_from_cstr("");
    }
}

double ps_value_to_number(const PSValue *v) {
    switch (v->type) {
        case PS_T_UNDEFINED:
            return 0.0 / 0.0; /* NaN */
        case PS_T_NULL:
            return 0.0;
        case PS_T_BOOLEAN:
            return v->as.boolean ? 1.0 : 0.0;
        case PS_T_NUMBER:
            return v->as.number;
        case PS_T_STRING:
            /* minimal version — improve later */
            return ps_string_to_number(v->as.string);
        case PS_T_OBJECT: {
            PSObject *obj = v->as.object;
            if (obj && obj->internal &&
                (obj->kind == PS_OBJ_KIND_STRING ||
                 obj->kind == PS_OBJ_KIND_NUMBER ||
                 obj->kind == PS_OBJ_KIND_BOOLEAN)) {
                PSValue *inner = (PSValue *)obj->internal;
                return ps_value_to_number(inner);
            }
            return 0.0;
        }
        default:
            return 0.0;
    }
}

/* Debug */

const char *ps_value_type_name(const PSValue *v) {
    switch (v->type) {
        case PS_T_UNDEFINED: return "undefined";
        case PS_T_NULL:      return "null";
        case PS_T_BOOLEAN:   return "boolean";
        case PS_T_NUMBER:    return "number";
        case PS_T_STRING:    return "string";
        case PS_T_OBJECT:    return "object";
        default:             return "unknown";
    }
}
