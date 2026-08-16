#include "lux.h"
#include "common.h"
#include "value.h"
#include "compiler.h"
#include "chunk.h"
#include "vm.h"
#include "memory.h"
#include "object.h"
#include "table.h"

#ifdef DEBUG_LOG_GC
#include "debug.h"
#endif

#define GC_HEAP_GROW_FACTOR 2

/* Prevent GC recursion during DEBUG_STRESS_GC + DEBUG_LOG_GC */
static bool gcInProgress = false;

/* Forward declarations */
void markCompilerRoots(void);
static void markArray(ValueArray* array);
static void blackenObject(Obj* object);
static void markRoots(void);
static void traceReferences(void);
static void sweep(void);
static void freeObject(Obj* object);

int 
grow_capacity(int capacity) 
{
    if (capacity < 8) return 8;
    return capacity * 2;
}

void* 
reallocate(void* pointer, ulong oldSize, ulong newSize) 
{
	vm.bytesAllocated += newSize - oldSize;
	if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
		if (!gcInProgress) collectGarbage();
#endif

		if (vm.bytesAllocated > vm.nextGC) {
		if (!gcInProgress) collectGarbage();
		}
	}

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

void 
markObject(Obj* object)
{
	if (object == nil) return;
	/* Defensive: catch misaligned or obviously-invalid pointers (e.g. from corrupted Values) */
	if ((uintptr)object & 7) {
		print("GC: invalid object pointer %p (misaligned)\n", object);
		return;
	}
	if (object->isMarked) return;
#ifdef DEBUG_LOG_GC
	print("%p mark ", (void*)object);
	printValue(OBJ_VAL(object));
	print("\n");
#endif

	object->isMarked = true;

	if (vm.grayCapacity < vm.grayCount +1) {
		vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
		vm.grayStack = (Obj**)realloc(vm.grayStack, sizeof(Obj*) * vm.grayCapacity);
		if (vm.grayStack == nil) exits("gray stack allocation failed");
	}
	
	vm.grayStack[vm.grayCount++] = object;
}


void 
markValue(Value value)
{
	if (IS_OBJ(value)) markObject(AS_OBJ(value));
}

static void 
markArray(ValueArray* array)
{
	for (int i = 0; i < array->count; i++) {
		markValue(array->values[i]);
	}
}

static void 
blackenObject(Obj* object)
{
#ifdef DEBUG_LOG_GC
	print("%p blacken ", (void*)object);
	printValue(OBJ_VAL(object));
	print("\n");
#endif

	switch (object->type) {
		case OBJ_ARRAY: {
			ObjArray* array;
			int i;
			
			array = (ObjArray*)object;
			for (i = 0; i < array->count; i++) {
				markValue(array->elements[i]);
			}
			break;
		}
		case OBJ_BOUND_METHOD: {
			ObjBoundMethod* bound = (ObjBoundMethod*)object;
			markValue(bound->receiver);
			markObject((Obj*)bound->method);
			break;
		}
		case OBJ_CLASS: {
			ObjClass* klass = (ObjClass*)object;
			markObject((Obj*)klass->name);
			markTable(&klass->methods);
			break;
		}		
		case OBJ_CLOSURE: {
			ObjClosure* closure;
			int i;
			
			closure = (ObjClosure*)object;
			markObject((Obj*)closure->function);
			if (closure->upvalues != nil) {
				for (i = 0; i < closure->upvalueCount; i++) 			{
					markObject((Obj*)closure->upvalues[i]);
				}
			}
			break;
		}
		case OBJ_FUNCTION: {
			ObjFunction* function = (ObjFunction*)object;
			markObject((Obj*)function->name);
			markArray(&function->chunk.constants);
			break;
		}
		case OBJ_INSTANCE: {
			ObjInstance* instance = (ObjInstance*)object;
			markObject((Obj*)instance->klass);
			markTable(&instance->fields);
			break;
		}
		case OBJ_UPVALUE:
			markValue(((ObjUpvalue*)object)->closed);
			break;
		case OBJ_NATIVE:
		case OBJ_STRING:
			break;
	}
}

static void 
freeObject(Obj* object)
{
#ifdef DEBUG_LOG_GC
	print("%p free type %d\n", (void*)object, object->type);
#endif

	switch (object->type) {
		case OBJ_ARRAY: {
			ObjArray* array = (ObjArray*)object;
			reallocate(array->elements, sizeof(Value) * array->capacity, 0);
			reallocate(object, sizeof(ObjArray), 0);
			break;
		}
		case OBJ_BOUND_METHOD:
			//FREE(ObjBoundMethod, object);
			reallocate(object, sizeof(ObjBoundMethod), 0);
			break;
		case OBJ_CLASS: {
			ObjClass* klass = (ObjClass*)object;
			freeTable(&klass->methods);
			//FREE(ObjClass, object);
			reallocate(object, sizeof(ObjClass), 0);
			break;
		}		
		case OBJ_CLOSURE: {
			ObjClosure* closure = (ObjClosure*)object;
			//FREE_ARRAY(ObjUpvalue*, closure->upvalues, closure->upvalueCount);
			//reallocate(closure->upvalues, closure->upvalueCount, 0);
			reallocate(closure->upvalues, sizeof(ObjUpvalue*) * closure->upvalueCount, 0);
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
		case OBJ_INSTANCE: {
			ObjInstance* instance = (ObjInstance*)object;
			freeTable(&instance->fields);
			//FREE(ObjInstance, object);
			reallocate(object, sizeof(ObjInstance), 0);
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

static void 
markRoots()
{
	for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
		markValue(*slot);
	}

	for (int i = 0; i < vm.frameCount; i++) {
		markObject((Obj*)vm.frames[i].closure);
	}

	for (ObjUpvalue* upvalue = vm.openUpvalues;
		upvalue != nil;
		upvalue= upvalue->next) {
		markObject((Obj*)upvalue);
	}

	markTable(&vm.globals);
	markCompilerRoots();
	markObject((Obj*)vm.initString);
	if (vm.scriptArgs != nil)
		markObject((Obj*)vm.scriptArgs);
	/* C-owned classes used by HTTP / Dict natives (not always in globals). */
	markServerRoots();
}

static void 
traceReferences()
{
	while (vm.grayCount > 0) {
		Obj* object = vm.grayStack[--vm.grayCount];
		blackenObject(object);
	}
}

static void 
sweep()
{
	Obj* previous = nil;
	Obj* object = vm.objects;
	while (object != nil) {
		if (object->isMarked) {
			previous = object;
			object->isMarked = false;
			object = object->next;
		} else {
			Obj* unreached = object;
			object = object->next;
			if (previous != nil) {
				previous->next = object;
			} else {
				vm.objects = object;
			}
			freeObject(unreached);
		}
	}
}

void 
collectGarbage(void) 
{
	if (gcInProgress) return; /* Prevent recursive GC */
	gcInProgress = true;
	
#ifdef DEBUG_LOG_GC
	print("-- gc begin\n");
	unsigned long before = vm.bytesAllocated;
#endif

	markRoots();
	traceReferences();
	tableRemoveWhite(&vm.strings);
	sweep();

	vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_LOG_GC
	print("-- gc end\n");
	print("    collected %uld bytes (from %uld to %uld) next at %uld\n",
		before - vm.bytesAllocated, before, vm.bytesAllocated,
		vm.nextGC);
#endif

	gcInProgress = false;
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

	free(vm.grayStack);
}
