#include "lux.h"
#include "common.h"
#include "value.h"
#include "chunk.h"
#include "vm.h"
#include "memory.h"
#include "object.h"
#include "table.h"


#define ALLOCATE_OBJ(type, objectType) \
	(type*)allocateObject(sizeof(type), objectType)

static Obj* 
allocateObject(ulong size, ObjType type) 
{
	Obj* object = (Obj*)reallocate(nil, 0, size);
	memset(object, 0, size);
	object->type = type;
	object->isMarked = false;

	object->next = vm.objects;
	vm.objects = object;

#ifdef DEBUG_LOG_GC
	print("%p allocate %uld for %d\n", (void*)object, size, type);
#endif

	return object;
}

ObjBoundMethod* 
newBoundMethod(Value receiver, ObjClosure* method)
{
	ObjBoundMethod* bound = ALLOCATE_OBJ(ObjBoundMethod, OBJ_BOUND_METHOD);
	bound->receiver= receiver;
	bound->method = method;
	return bound;
}


ObjClass* 
newClass(ObjString* name)
{
	ObjClass* klass = ALLOCATE_OBJ(ObjClass, OBJ_CLASS);
	klass->name = name;
	initTable(&klass->methods);
	return klass;
}


ObjClosure* 
newClosure(ObjFunction* function) 
{
	ObjClosure* closure;
	int i;
	
	closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
	closure->function = function;
	closure->upvalueCount = function->upvalueCount;
	closure->upvalues = nil; /* Initialize to nil before allocation */
	
	closure->upvalues = ALLOCATE(ObjUpvalue*, function->upvalueCount);
	for (i = 0; i < closure->upvalueCount; i++) {
		closure->upvalues[i] = nil;
	}
	
	return closure;
}

ObjFunction* 
newFunction()
{
	ObjFunction* function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
	function->arity = 0;
	function->upvalueCount = 0;
	function->name = nil;
	initChunk(&function->chunk);
	return function;
}

ObjInstance* 
newInstance(ObjClass* klass)
{
	ObjInstance* instance = ALLOCATE_OBJ(ObjInstance, OBJ_INSTANCE);
	instance->klass = klass;
	initTable(&instance->fields);
	return instance;
}

ObjNative* 
newNative(NativeFn function)
{
	ObjNative* native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
	native->function = function;
	return native;
}

ObjArray*
newArray(void)
{
	ObjArray* array = ALLOCATE_OBJ(ObjArray, OBJ_ARRAY);
	array->count = 0;
	array->capacity = 0;
	array->elements = nil;
	return array;
}

void
writeArray(ObjArray* array, Value value)
{
	int oldCapacity;
	Value* newElements;
	
	if (array->capacity < array->count + 1) {
		oldCapacity = array->capacity;
		array->capacity = oldCapacity < 8 ? 8 : oldCapacity * 2;
		newElements = (Value*)reallocate(array->elements, 
			sizeof(Value) * oldCapacity, 
			sizeof(Value) * array->capacity);
		array->elements = newElements;
	}
	array->elements[array->count] = value;
	array->count++;
}

static ObjString* 
allocateString(char* chars, int length, unsigned long hash)
{
	ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
	string->length = length;
	string->chars = chars;
	string->hash = hash;

	push(OBJ_VAL(string));
	tableSet(&vm.strings, string, NIL_VAL);
	pop();

	return string;
}

static unsigned long 
hashString(char* key, int length)
{
    unsigned long hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (unsigned char)key[i];
        hash *= 1677619;
    }
    return hash;
}

ObjString* 
takeString(char* chars, int length) 
{
	unsigned long hash = hashString(chars, length);
	ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
	if (interned != nil) {
		reallocate(chars, length + 1, 0);
		return interned;
	}
	return allocateString(chars, length, hash);
}

ObjString* 
copyString(const char* chars, int length) 
{
	unsigned long hash = hashString(chars, length);
	ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
	if (interned != nil) return interned;
	char* heapChars = ALLOCATE(char, length + 1);
	memcpy(heapChars, chars, length);
	heapChars[length] = '\0';
	return allocateString(heapChars, length, hash);
}

ObjString*
valueToString(Value value)
{
	if (IS_BOOL(value)) {
		if (AS_BOOL(value)) {
			return copyString("true", 4);
		} else {
			return copyString("false", 5);
		}
	} else if (IS_NIL(value)) {
		return copyString("nil", 3);
	} else if (IS_NUMBER(value)) {
		char buffer[32];
		int length = snprint(buffer, sizeof(buffer), "%.15g", AS_NUMBER(value));
		return copyString(buffer, length);
	} else if (IS_STRING(value)) {
		return AS_STRING(value);
	}
	/* For other objects, return a simple representation */
	return copyString("[object]", 8);
}

ObjUpvalue* 
newUpvalue(Value* slot) 
{
	ObjUpvalue* upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
	upvalue->closed = NIL_VAL;
	upvalue->location = slot;
	upvalue->next = nil;
	return upvalue;
}


static void 
printFunction(ObjFunction* function)
{
	if (function->name == nil) {
		print("<script>");
		return;
	}
	print("<fn %s>", function->name->chars);
}

void printObject(Value value) 
{
	int i;
	switch (OBJ_TYPE(value)) {
		case OBJ_ARRAY: {
			ObjArray* array = AS_ARRAY(value);
			print("[");
			for (i = 0; i < array->count; i++) {
				printValue(array->elements[i]);
				if (i < array->count - 1) {
					print(", ");
				}
			}
			print("]");
			break;
		}
		case OBJ_BOUND_METHOD:
			printFunction(AS_BOUND_METHOD(value)->method->function);
			break;
		case OBJ_CLASS:
			print("%s", AS_CLASS(value)->name->chars);
			break;
		case OBJ_CLOSURE:
			printFunction(AS_CLOSURE(value)->function);
			break;
		case OBJ_FUNCTION:
			printFunction(AS_FUNCTION(value));
			break;
		case OBJ_INSTANCE:
			print("%s instance", AS_INSTANCE(value)->klass->name->chars);
			break;
		case OBJ_NATIVE:
			print("<native fn>");
			break;
		case OBJ_STRING:
			print("%s", AS_CSTRING(value));
			break;
		case OBJ_UPVALUE:
			print("upvalue");
			break;
	}
}
