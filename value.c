#include "lux.h"
#include "types.h"
#include "common.h"
#include "value.h"
#include "chunk.h"
#include "vm.h"
#include "memory.h"
#include "object.h"

Value
obj_val(Obj* object)
{
	Value v;
	v.type = VAL_OBJ;
	v.as.obj = object;
	return v;
}

Value
nil_val(void)
{
	Value v;
	v.type = VAL_NIL;
	v.as.number = 0;
	return v;
}

Value
bool_val(int boolean)
{
	Value v;
	v.type = VAL_BOOL;
	v.as.boolean = boolean;
	return v;
}

Value
number_val(double number)
{
	Value v;
	v.type = VAL_NUMBER;
	v.as.number = number;
	return v;
}

void
initValueArray(ValueArray* array)
{
	array->values = nil;
	array->capacity = 0;
	array->count = 0;
}

void 
writeValueArray(ValueArray* array, Value value) 
{
	if (array->capacity < array->count + 1) {
		int oldCapacity = array->capacity;
		// array->capacity = GROW_CAPACITY(oldCapacity); posix
		array->capacity = grow_capacity(oldCapacity);
		//array->values = GROW_ARRAY(Value, array->values, oldCapacity, array->capacity);
		array->values = (Value*)reallocate(array->values, 
								(ulong)(oldCapacity * sizeof(Value)),
								(ulong)(array->capacity * sizeof(Value)));
	}
	array->values[array->count] = value;
	array->count++;
}

void
freeValueArray(ValueArray* array)
{
	//FREE_ARRAY(Value, array->values, array-capacity);
	reallocate(array->values, (ulong)(array->capacity * sizeof(Value)), 0);
	initValueArray(array);
}

void 
printValue(Value value) {
	switch (value.type) {
		case VAL_BOOL:
			print(AS_BOOL(value) ? "true" : "false");
			break;
		case VAL_NIL: print("nil"); break;
		case VAL_NUMBER: print("%g", AS_NUMBER(value)); break;
		case VAL_OBJ: printObject(value); break;
	}
}

bool 
valuesEqual(Value a, Value b) {
	/* DECLARE at the very top of the function block */
	ObjString* aString;
	ObjString* bString;

	if (a.type != b.type) return false;
	
	switch (a.type) {
		case VAL_BOOL:   return AS_BOOL(a) == AS_BOOL(b);
		case VAL_NIL:    return true;
		case VAL_NUMBER: return AS_NUMBER(a) == AS_NUMBER(b);
		case VAL_OBJ:
			/* ASSIGN values here */
			aString = AS_STRING(a);
			bString = AS_STRING(b);
			return aString->length == bString->length &&
				memcmp(aString->chars, bString->chars,
					aString->length) == 0;
		default: return false;
	}
}