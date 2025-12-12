#ifndef lux_chunk_h
#define lux_chunk_h

#include "value.h"

typedef enum {
	OP_CONSTANT,
	OP_RETURN,
} OpCode;

                             // dynamic array of instructions, we don't know the size yet
                             // unsigned char code; for plan9
typedef struct {
	int count;               // allocated entries in use
	int capacity;            // the elements
    // [1,4,9,x] count=3 capacity=4, we have space for one more
	// uint8_t* code on posix
    uchar *code;             // pointer to dynamically allocated array
	int* lines;
	ValueArray constants;
} Chunk;

                           
void initChunk(Chunk* chunk); // declare our function for use
void freeChunk(Chunk* chunk); // free we mange this
void writeChunk(Chunk* chunk, uchar byte, int line); // append to end
int addConstant(Chunk* chunk, Value value);

#endif