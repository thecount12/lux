#ifndef lux_memory_h
#define lux_memory_h

#include "common.h"
#include "object.h"

#define ALLOCATE(type, count) \
		(type*)reallocate(NULL, 0, sizeof(type) * (count))

#define FREE(type, pointer) reallocate(pointer, sizeof(type), 0)

 #define GROW_CAPACITY(capacity) \
    ((capacity) < 8 ? 8 : (capacity) * 2) // grow by factor of 2 sometimes 1.5

/* once we have desired capacity we grow the array size */
#define GROW_ARRAY(type, pointer, oldCount, newCount) \
    (type*)reallocate(pointer, sizeof(type) * (oldCount), \
        sizeof(type) * (newCount))

#define FREE_ARRAY(type, pointer, oldCount) \
    reallocate(pointer, sizeof(type) * (oldCount), 0)

        /* now we put this into action reallocate in memory.c */
void* reallocate(void* pointer, size_t oldSize, size_t newSize);
void markObject(Obj* object);
void markValue(Value value);
void collectGarbage();
void freeObjects();

#endif
