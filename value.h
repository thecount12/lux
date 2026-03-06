#ifndef lux_value_h
#define lux_value_h

/* Value and ValueArray are defined in common.h to provide a single canonical
 * representation for all translation units. This header provides the APIs that
 * operate on those types. */

#ifndef NAN_BOXING
Value obj_val(Obj* v);

/* Cast to double handles union initialization for either field */
Value bool_val(int v);
Value nil_val(void);
Value number_val(double v);

#define BOOL_VAL(v) (bool_val(v))
#define NIL_VAL     (nil_val())
#define NUMBER_VAL(v) (number_val(v))
#define OBJ_VAL(object) (obj_val((Obj*)(object)))
#endif

bool valuesEqual(Value a, Value b);
void initValueArray(ValueArray* array);
void writeValueArray(ValueArray* array, Value value);
void freeValueArray(ValueArray* array);
void printValue(Value value);

#endif
