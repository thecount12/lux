#ifndef lux_chunk_h
#define lux_chunk_h

#include "value.h"

typedef enum {
	OP_CONSTANT,
	OP_NIL,
	OP_TRUE,
	OP_FALSE,
	OP_EQUAL,
	OP_GREATER,
	OP_LESS,
	OP_ADD,
	OP_SUBTRACT,
	OP_MULTIPLY,
	OP_DIVIDE,
	OP_NOT,
	OP_NEGATE,
	OP_RETURN, 
} OpCode;

/*                        
typedef struct {
	int count; 
	int capacity;        
  
    uchar *code;
	int* lines;
	ValueArray constants;
} Chunk;
*/
typedef struct Chunk Chunk; /* Explicitly link the tag to the typedef */
struct Chunk {
    int count; 
    int capacity;        
    uchar *code;
    int* lines;
    ValueArray constants;
};
                           
void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uchar byte, int line);
int addConstant(Chunk* chunk, Value value);

#endif
