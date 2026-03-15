#include <stdio.h>
#include <string.h>
#include <float.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, objectType) \
	(type*)allocateObject(sizeof(type), objectType)

static Obj* allocateObject(size_t size, ObjType type) {
	Obj* object = (Obj*)reallocate(NULL, 0, size);
	object->type = type;

	object->next = vm.objects;
	vm.objects = object;

#ifdef DEBUG_LOG_GC
	printf("%p allocate %zu for %d\n", (void*)object, size, type);
#endif

	return object;
}

ObjBoundMethod* newBoundMethod(Value receiver, ObjClosure* method) {
	ObjBoundMethod* bound = ALLOCATE_OBJ(ObjBoundMethod, OBJ_BOUND_METHOD);
	bound->receiver= receiver;
	bound->method = method;
	return bound;
}

ObjClass* newClass(ObjString* name) {
	ObjClass* klass = ALLOCATE_OBJ(ObjClass, OBJ_CLASS);
	klass->name = name;
	initTable(&klass->methods);
	return klass;
}

ObjClosure* newClosure(ObjFunction* function) {
	ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*,
                                   function->upvalueCount);
	for (int i = 0; i < function->upvalueCount; i++) {
		upvalues[i] = NULL;
	}

	ObjClosure* closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
	closure->function = function;
	closure->upvalues = upvalues;
	closure->upvalueCount = function->upvalueCount;
	return closure;
}

ObjFunction* newFunction() {
	ObjFunction* function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
	function->arity = 0;
	function->upvalueCount = 0;
	function->name = NULL;
	initChunk(&function->chunk);
	return function;
}

ObjInstance* newInstance(ObjClass* klass) {
	ObjInstance* instance = ALLOCATE_OBJ(ObjInstance, OBJ_INSTANCE);
	instance->klass = klass;
	initTable(&instance->fields);
	return instance;
}

ObjNative* newNative(NativeFn function) {
	ObjNative* native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
	native->function = function;
	return native;
}

ObjArray* newArray(void) {
	ObjArray* array = ALLOCATE_OBJ(ObjArray, OBJ_ARRAY);
	array->count = 0;
	array->capacity = 0;
	array->elements = NULL;
	return array;
}

ObjFloatArray* newFloatArray(int length) {
	ObjFloatArray* arr = ALLOCATE_OBJ(ObjFloatArray, OBJ_FLOATARRAY);
	arr->length = length;
	if (length > 0) {
		arr->elems = ALLOCATE(double, length);
		for (int i = 0; i < length; i++) arr->elems[i] = 0.0;
	} else {
		arr->elems = NULL;
	}
	return arr;
}

void writeArray(ObjArray* array, Value value) {
	if (array->capacity < array->count + 1) {
		int oldCapacity = array->capacity;
		array->capacity = oldCapacity < 8 ? 8 : oldCapacity * 2;
		array->elements = GROW_ARRAY(Value, array->elements, oldCapacity, array->capacity);
	}
	array->elements[array->count] = value;
	array->count++;
}

static ObjString* allocateString(char* chars, int length, uint32_t hash) {
	ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
	string->length = length;
	string->chars = chars;
	string->hash = hash;

	push(OBJ_VAL(string));
	tableSet(&vm.strings, string, NIL_VAL);
	pop();

	return string;
}

static uint32_t hashString(const char* key, int length) {
	uint32_t hash = 2166136261u;
	for (int i = 0; i < length; i++) {
		hash ^= (uint8_t)key[i];
		hash *= 16777619;
	}
	return hash;
}

ObjString* takeString(char* chars, int length) {
	uint32_t hash = hashString(chars, length);
	ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
	if (interned != NULL) {
		FREE_ARRAY(char, chars, length +1);
		return interned;
	}
	return allocateString(chars, length, hash);
}

ObjString* copyString(const char* chars, int length) {
	uint32_t hash = hashString(chars, length);
	ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
	if (interned != NULL) return interned;
	char* heapChars = ALLOCATE(char, length + 1);
	memcpy(heapChars, chars, length);
	heapChars[length] = '\0';
	return allocateString(heapChars, length, hash);
}

ObjString* valueToString(Value value) {
	if (IS_BOOL(value)) {
		if (AS_BOOL(value)) {
			return copyString("true", 4);
		} else {
			return copyString("false", 5);
		}
	} else if (IS_NIL(value)) {
		return copyString("nil", 3);
	} else if (IS_NUMBER(value)) {
		/* Use DBL_DECIMAL_DIG for round-trip accuracy, fallback to 17 if missing. */
#ifndef DBL_DECIMAL_DIG
#define DBL_DECIMAL_DIG 17
#endif
		char buffer[64];
		int length = snprintf(buffer, sizeof(buffer), "%.*g", DBL_DECIMAL_DIG, AS_NUMBER(value));
		if (length < 0) length = 0;

		/* If output was truncated, allocate a larger buffer and reformat. */
		if (length >= (int)sizeof(buffer)) {
			char* big = ALLOCATE(char, length + 1);
			if (big != NULL) {
				/* snprintf with exact size (length+1) to include NUL */
				snprintf(big, (size_t)length + 1, "%.*g", DBL_DECIMAL_DIG, AS_NUMBER(value));
				ObjString* s = copyString(big, length);
				FREE_ARRAY(char, big, length + 1);
				return s;
			}
			/* Fall through to return truncated buffer if allocation fails */
		}
		return copyString(buffer, length);
	} else if (IS_STRING(value)) {
		return AS_STRING(value);
	}
	/* For other objects, return a simple representation */
	return copyString("[object]", 8);
}

ObjUpvalue* newUpvalue(Value* slot) {
	ObjUpvalue* upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
	upvalue->closed = NIL_VAL;
	upvalue->location = slot;
	upvalue->next = NULL;
	return upvalue;
}

static void printFunction(ObjFunction* function) {
	if (function->name == NULL) {
		printf("<script>");
		return;
	}
	printf("<fn %s>", function->name->chars);
}

void printObject(Value value) {
	switch (OBJ_TYPE(value)) {
		case OBJ_ARRAY: {
			ObjArray* array = AS_ARRAY(value);
			printf("[");
			for (int i = 0; i < array->count; i++) {
				printValue(array->elements[i]);
				if (i < array->count - 1) {
					printf(", ");
				}
			}
			printf("]");
			break;
		}
		case OBJ_BOUND_METHOD:
			printFunction(AS_BOUND_METHOD(value)->method->function);
			break;
		case OBJ_CLASS:
			printf("%s", AS_CLASS(value)->name->chars);
			break;
		case OBJ_CLOSURE:
			printFunction(AS_CLOSURE(value)->function);
			break;
		case OBJ_FUNCTION:
			printFunction(AS_FUNCTION(value));
			break;
		case OBJ_INSTANCE: {
			ObjInstance* inst = AS_INSTANCE(value);
			if (inst->klass == NULL) {
				/* Print as dict/object */
				printf("{");
				bool first = true;
				for (int i = 0; i < inst->fields.capacity; i++) {
					Entry* entry = &inst->fields.entries[i];
					if (entry->key != NULL) {
						if (!first) printf(", ");
						printf("\"%s\": ", entry->key->chars);
						printValue(entry->value);
						first = false;
					}
				}
				printf("}");
			} else {
				printf("%s instance", inst->klass->name->chars);
			}
			break;
		}
		case OBJ_NATIVE:
			printf("<native fn>");
			break;
		case OBJ_STRING:
			printf("%s", AS_CSTRING(value));
			break;
		case OBJ_FLOATARRAY: {
			ObjFloatArray* fa = (ObjFloatArray*)AS_OBJ(value);
			printf("Float64Array(len=%d)", fa->length);
			break;
		}
		case OBJ_UPVALUE:
			printf("upvalue");
			break;
	}
}
