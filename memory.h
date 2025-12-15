#ifndef lux_memory_h
#define lux_memory_h

int grow_capacity(int capacity);
void* reallocate(void* pointer, ulong oldSize, ulong newSize);


#endif
