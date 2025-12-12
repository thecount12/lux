#include "lux.h"
#include "memory.h"


int grow_capacity(int capacity) {
    if (capacity < 8) return 8;
    return capacity * 2;
}

void* 
reallocate(void* pointer, ulong oldSize, ulong newSize) {
    if (newSize == 0) {
        free(pointer);
        return nil;
    }
    void* result = realloc(pointer, newSize);
    if (result == nil) {
        fprint(2, "Fatal error: realloc failed\n");
        exits("realloc failed"); 
    }
    return result; 
}

/* posix only
void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
    if (newSize == 0) {
        free(pointer);
        return NULL;
    }
    void* result = realloc(pointer, newSize);
    if (result == NULL) exit(1);
    return result; 
}
*/