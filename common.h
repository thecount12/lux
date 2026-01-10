#ifndef lux_common_h
#define lux_common_h

#include "types.h"

#ifndef lux_bool_defined
#define lux_bool_defined
typedef int bool;
#define true 1
#define false 0
#endif

#define DEBUG_PRINT_CODE
#define DEBUG_TRACE_EXECUTION

#define IS_BOOL(v)    ((v).type == VAL_BOOL)
#define IS_NIL(v)     ((v).type == VAL_NIL)
#define IS_NUMBER(v)  ((v).type == VAL_NUMBER)
#define IS_OBJ(v)     ((v).type == VAL_OBJ)

#define AS_OBJ(v)     ((v).as.obj)
#define AS_BOOL(v)    ((v).as.boolean)
#define AS_NUMBER(v)  ((v).as.number)

extern VM vm;

#endif

