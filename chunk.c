#include "lux.h"
#include "types.h"
#include "common.h"
#include "value.h"
#include "chunk.h"
#include "vm.h"
#include "memory.h"

void 
initChunk(Chunk* chunk) 
{
    chunk->count = 0;
    chunk->capacity = 0;
	chunk->code = (uchar*)nil; // posix chunk->code = NULL
	chunk->lines = (int*)nil;
	initValueArray(&chunk->constants);
}

/* append byte to end of chunk */
void 
writeChunk(Chunk* chunk, uchar byte, int line) 
{
    if (chunk->capacity < chunk->count +1) {
        int oldCapacity = chunk->capacity;
		// chunk->capacity = GROW_CAPACITY(oldCapacity); posix
        chunk->capacity = grow_capacity(oldCapacity);
		// chunk->code = GROW_ARRAY(uint8_t, chunk->code, oldCapacity, chunk->capacity); posix
        chunk->code = (uchar*)reallocate(chunk->code, 
                                        (ulong)(oldCapacity * sizeof(uchar)), 
                                        (ulong)(chunk->capacity * sizeof(uchar)));
		// chunk-lines = GROW_ARRAY(int, chunk->lines, oldCapacity, chunk->capacity);
		chunk->lines = (int*)reallocate(chunk->lines, 
										(ulong)(oldCapacity * sizeof(int)),
										(ulong)(chunk->capacity * sizeof(int)));
    }
    chunk->code[chunk->count] = byte;
	chunk->lines[chunk->count] = line;
    chunk->count++;
}

int
addConstant(Chunk* chunk, Value value) {
	writeValueArray(&chunk->constants, value);
	return chunk->constants.count -1;
}

void 
freeChunk(Chunk* chunk) 
{
	// FREE_ARRAY(uint8_t, chunk->code, chunk->capacity); posix
	reallocate(chunk->code, (ulong)(chunk->capacity * sizeof(uchar)), 0);
	// FREE_ARRAY(int, chunk->lines, chunk->capacity); posix
	reallocate(chunk->lines, (ulong)(chunk->capacity * sizeof(int)), 0);
	freeValueArray(&chunk->constants);
    initChunk(chunk);
}
