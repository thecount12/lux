#ifndef lux_common_h
#define lux_common_h

#include "types.h"

#ifndef lux_bool_defined
#define lux_bool_defined
typedef int bool;
#define true 1
#define false 0
#endif

//#define DEBUG_PRINT_CODE
//#define DEBUG_TRACE_EXECUTION

//#define DEBUG_STRESS_GC
//#define DEBUG_LOG_GC

//#define UINT8_MAX 255
//#define UINT8_COUNT (UINT8_MAX + 1)

#define IS_BOOL(v)    ((v).type == VAL_BOOL)
#define IS_NIL(v)     ((v).type == VAL_NIL)
#define IS_NUMBER(v)  ((v).type == VAL_NUMBER)
#define IS_OBJ(v)     ((v).type == VAL_OBJ)
#define IS_STRING(v)  (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_STRING)

#define AS_OBJ(v)     ((v).as.obj)
#define AS_BOOL(v)    ((v).as.boolean)
#define AS_NUMBER(v)  ((v).as.number)
#define AS_STRING(v)  ((ObjString*)AS_OBJ(v))

extern VM vm;

#endif

