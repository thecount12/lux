#ifndef lux_chunk_h
#define lux_chunk_h

/* chunk.h - function declarations only 
 * Assumes: types.h has been included by the .c file
 */

void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uchar byte, int line);
int addConstant(Chunk* chunk, Value value);

#endif
