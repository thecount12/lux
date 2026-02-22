#include "lux.h"
#include "types.h"
#include "common.h"
#include "value.h"
#include "chunk.h"
#include "vm.h"
#include "memory.h"
#include "object.h"

int 
grow_capacity(int capacity) 
{
    if (capacity < 8) return 8;
    return capacity * 2;
}

void* 
reallocate(void* pointer, ulong oldSize, ulong newSize) 
{
	USED(oldSize); //temp fix for not sued warning 
    if (newSize == 0) {
        free(pointer);
        return nil;
    }
    void* result = realloc(pointer, newSize);
    if (result == nil) {
        print("Fatal error: realloc failed\n");
        exits("realloc failed"); 
    }
    return result; 
}

static void 
freeObject(Obj* object) 
{
	switch (object->type) {
		case OBJ_CLOSURE: {
			ObjClosure* closure = (ObjClosure*)object;
			//FREE_ARRAY(ObjUpvalue*, closure->upvalues, closure->upvalueCount);
			reallocate(closure->upvalues, closure->upvalueCount, 0);
			//FREE(ObjClosure, object); // posix
			reallocate(object, sizeof(ObjClosure), 0);
			break;
		}
		case OBJ_FUNCTION: {
			ObjFunction* function = (ObjFunction*)object;
			freeChunk(&function->chunk);
			FREE(ObjFunction, object);
			break;
		}
		case OBJ_NATIVE:
		//FREE(ObjNative, object);
		reallocate(object, sizeof(ObjNative), 0);
		break;
		case OBJ_STRING: {
			ObjString* string = (ObjString*)object;
			//FREE_ARRAY(char, string->chars, string->length + 1);	
			//FREE(ObjString, object);
			// plan9
			reallocate(string->chars, string->length + 1, 0);	
			reallocate(object, sizeof(ObjString), 0);
			break;
			break;
		}
		case OBJ_UPVALUE:
			//FREE(ObjUpvalue, object);
			reallocate(object, sizeof(ObjUpvalue), 0);
			break;
	}
}

void 
freeObjects(void) 
{
	Obj* object = vm.objects;
	while (object != nil) {
		Obj* next = object->next;
		freeObject(object);
		object = next;
	}
}
