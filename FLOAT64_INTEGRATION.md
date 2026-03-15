# Float64Array Integration Steps

Add Float64Array support to the Plan 9 build by following these steps.

## 1. types.h

Add `OBJ_FLOATARRAY` to the `ObjType` enum and define `struct ObjFloatArray`:

```c
/* In ObjType enum, after OBJ_ARRAY: */
	OBJ_FLOATARRAY,

/* After struct ObjArray, before struct ObjUpvalue: */
struct ObjFloatArray {
	Obj obj;
	int length;
	double* elems;
};

/* In forward declarations: */
typedef struct ObjFloatArray ObjFloatArray;
```

## 2. object.h

Add macros and declarations:

```c
#define IS_FLOATARRAY(value) isObjType(value, OBJ_FLOATARRAY)
#define AS_FLOATARRAY(value) ((ObjFloatArray*)AS_OBJ(value))

ObjFloatArray* newFloatArray(int length);
void registerFloat64Natives(void (*defineNative)(const char*, Value (*)(int, Value*)));
```

## 3. object.c

Add `newFloatArray` after `newArray`:

```c
ObjFloatArray*
newFloatArray(int length)
{
	ObjFloatArray* arr = ALLOCATE_OBJ(ObjFloatArray, OBJ_FLOATARRAY);
	arr->length = length;
	if (length > 0) {
		arr->elems = ALLOCATE(double, length);
		for (int i = 0; i < length; i++) arr->elems[i] = 0.0;
	} else {
		arr->elems = nil;
	}
	return arr;
}
```

Add `OBJ_FLOATARRAY` case to `valueToString`:

```c
		case OBJ_FLOATARRAY: {
			ObjFloatArray* fa = (ObjFloatArray*)AS_OBJ(value);
			print("Float64Array(len=%d)", fa->length);
			break;
		}
```

## 4. memory.c

Add `OBJ_FLOATARRAY` to `blackenObject`:

```c
		case OBJ_NATIVE:
		case OBJ_STRING:
		case OBJ_FLOATARRAY:
			break;
```

Add `OBJ_FLOATARRAY` case to `freeObject`:

```c
		case OBJ_FLOATARRAY: {
			ObjFloatArray* fa = (ObjFloatArray*)object;
			if (fa->elems != nil) reallocate(fa->elems, sizeof(double) * fa->length, 0);
			reallocate(object, sizeof(ObjFloatArray), 0);
			break;
		}
```

## 5. vm.c

Add at top (after includes):

```c
#include "float64.h"
void registerFloat64Natives(void (*)(const char*, Value (*)(int, Value*)));
```

In `initVM`, replace the `float64_available` defineNative with:

```c
	registerFloat64Natives(defineNative);
```

Remove the inline `float64AvailableNative` function.

## 6. Mkfile

Add to OFILES:

```
	float64.$O\
```

Add to HFILES:

```
	float64.h\
```

## Note: Plan 9 8c

Plan 9's 8c compiler may report "name not declared: ObjFloatArray" when compiling `float64.c`. If that occurs, the Float64Array natives must be moved into `object.c` or `memory.c` instead of using the separate `float64.c` module.
