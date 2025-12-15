#ifndef lux_vm_h
#define lux_vm_h

#include "chunk.h"

typedef struct {
	Chunk* chunk;
	// int* ip; //uint8_t* ip posix
	uchar* ip;
} VM;

typedef enum {
	INTERPRET_OK,
	INTERPRET_COMPILE_ERROR,
	INTERPRET_RUNTIME_ERROR
} InterpretResult;


void initVM();

void freeVM();

InterpretResult interpret(Chunk* chunk);

#endif
