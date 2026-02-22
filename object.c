#include "lux.h"
#include "types.h"
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
	object->type = type;


	object->next = vm.objects;
	vm.objects = object;
	return object;
}

ObjClosure* 
newClosure(ObjFunction* function) 
{
	ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*, function->upvalueCount);
	for (int i = 0; i < function->upvalueCount; i++) {
		upvalues[i] = nil;
	}

	ObjClosure* closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
	closure->function = function;
	closure->upvalues = upvalues;
	closure->upvalueCount = function->upvalueCount;
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

ObjNative* 
newNative(NativeFn function)
{
	ObjNative* native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
	native->function = function;
	return native;
}

static ObjString* 
allocateString(char* chars, int length, unsigned long hash)
{
	ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
	string->length = length;
	string->chars = chars;
	string->hash = hash;
	tableSet(&vm.strings, string, NIL_VAL);
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
	switch (OBJ_TYPE(value)) {
		case OBJ_CLOSURE:
			printFunction(AS_CLOSURE(value)->function);
			break;
		case OBJ_FUNCTION:
			printFunction(AS_FUNCTION(value));
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
