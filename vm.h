#ifndef lux_vm_h
#define lux_vm_h

#include "chunk.h"
#include "value.h"

#define STACK_MAX 256

typedef struct {
	Chunk* chunk;
	// int* ip; //uint8_t* ip posix
	uchar* ip;
	Value stack[STACK_MAX];
	Value* stackTop;
} VM;

typedef enum {
	INTERPRET_OK,
	INTERPRET_COMPILE_ERROR,
	INTERPRET_RUNTIME_ERROR
} InterpretResult;


void initVM();

void freeVM();

InterpretResult interpret(char* source);
void push(Value value);
Value pop();

#endif
