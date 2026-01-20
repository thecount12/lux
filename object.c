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

void printObject(Value value) 
{
	switch (OBJ_TYPE(value)) {
		case OBJ_STRING:
			print("%s", AS_CSTRING(value));
			break;
	}
}
