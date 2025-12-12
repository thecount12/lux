#include "lux.h"
#include "memory.h"
#include "value.h"


int grow_capacity(int capacity);
void* reallocate(void* pointer, ulong oldSize, ulong newSize);

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
printValue(Value value)
{
	print("%g", value);
}
