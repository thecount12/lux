#ifndef lux_memory_h
#define lux_memory_h

int grow_capacity(int capacity);
void* reallocate(void* pointer, ulong oldSize, ulong newSize);

/* posix only
#define GROW_CAPACITY(capacity) \
    ((capacity) < 8 ? 8 : (capacity) * 2) // grow by factor of 2 sometimes 1.5
#define GROW_ARRAY(type, pointer, oldCount, newCount) \
    (type*)reallocate(pointer, sizeof(type) * (oldCount), \
        sizeof(type) * (newCount))
#define FREE_ARRAY(type, pointer, oldCount) \
    reallocate(pointer, sizeof(type) * (oldCount), 0)
void* reallocate(void* pointer, size_t oldSize, size_t newSize);
*/

#endif
