#ifndef PS_VALUE_H
#define PS_VALUE_H

#include <stddef.h>
#include <stdint.h>

/* Forward declarations */
struct PSObject;
struct PSString;

/* ECMAScript value types */
typedef enum {
    PS_T_UNDEFINED,
    PS_T_NULL,
    PS_T_BOOLEAN,
    PS_T_NUMBER,
    PS_T_STRING,
    PS_T_OBJECT
} PSValueType;

/* Tagged value */
typedef struct {
    PSValueType type;
    union {
        int              boolean;  /* 0 or 1 */
        double           number;   /* IEEE-754 */
        struct PSString *string;
        struct PSObject *object;
    } as;
} PSValue;

/* Constructors */
PSValue ps_value_undefined(void);
PSValue ps_value_null(void);
PSValue ps_value_boolean(int value);
PSValue ps_value_number(double value);
PSValue ps_value_string(struct PSString *value);
PSValue ps_value_object(struct PSObject *value);

/* Type checks */
int ps_value_is_primitive(const PSValue *v);
int ps_value_is_truthy(const PSValue *v);

/* Conversions (ES1-style) */
struct PSString *ps_value_to_string(const PSValue *v);
double           ps_value_to_number(const PSValue *v);

/* Debug / internal */
const char *ps_value_type_name(const PSValue *v);

#endif /* PS_VALUE_H */
