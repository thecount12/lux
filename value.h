#ifndef lux_value_h
#define lux_value_h

#include "common.h"

/* Plan 9 Fix: Anonymous enum prevents "redeclare tag" conflicts */
typedef enum {
	VAL_BOOL,
	VAL_NIL,
	VAL_NUMBER,
} ValueType;

typedef struct Value Value;
struct Value {
	ValueType type;
	union {
		int	boolean;
		double	number;
	} as;
};

#define IS_BOOL(v)	((v).type == VAL_BOOL)
#define IS_NIL(v)	((v).type == VAL_NIL)
#define IS_NUMBER(v)	((v).type == VAL_NUMBER)

#define AS_BOOL(v)	((v).as.boolean)
#define AS_NUMBER(v)	((v).as.number)

/* Cast to double handles union initialization for either field */
Value	bool_val(int v);
Value	nil_val(void);
Value	number_val(double v);

#define BOOL_VAL(v)	(bool_val(v))
#define NIL_VAL		(nil_val())
#define NUMBER_VAL(v)	(number_val(v))

typedef struct ValueArray ValueArray;
struct ValueArray {
	int	capacity;
	int	count;
	Value*	values;
};

bool	valuesEqual(Value a, Value b);
void	initValueArray(ValueArray* array);
void	writeValueArray(ValueArray* array, Value value);
void	freeValueArray(ValueArray* array);
void	printValue(Value value);

#endif