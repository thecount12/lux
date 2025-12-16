#ifndef lux_chunk_h
#define lux_chunk_h

#include "value.h"

typedef enum {
	OP_CONSTANT,
	OP_ADD,
	OP_SUBTRACT,
	OP_MULTIPLY,
	OP_DIVIDE,
	OP_NEGATE,
	OP_RETURN,
} OpCode;

                          
typedef struct {
	int count; 
	int capacity;        
  
    uchar *code;
	int* lines;
	ValueArray constants;
} Chunk;

                           
void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uchar byte, int line);
int addConstant(Chunk* chunk, Value value);

#endif
