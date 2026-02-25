#ifndef lux_memory_h
#define lux_memory_h

int grow_capacity(int capacity);
void* reallocate(void* pointer, ulong oldSize, ulong newSize);
void markObject(Obj* object);
void markValue(Value value);
void collectGarbage(void);
void freeObjects(void);

#define GROW_CAPACITY(capacity) grow_capacity(capacity)

#define ALLOCATE(type, count) \
	(type*)reallocate(nil, 0, sizeof(type) * (count))

#define FREE(type, pointer) reallocate(pointer, sizeof(type), 0)

#endif
