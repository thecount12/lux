#include "lux.h"
#include "chunk.h"
#include "debug.h"
#include "vm.h"

void initVM(void);
/*
void disassembleChunk(Chunk* chunk, const char* name);
void initvm();
void freeVM();
InterpretResult interpret(Chunk* chunk);
*/


int 
main()
{
	initVM();
	
	Chunk chunk;
	initChunk(&chunk);

	int constant = addConstant(&chunk, 1.2);
	writeChunk(&chunk, OP_CONSTANT, 123);
	writeChunk(&chunk, constant, 123);

	constant = addConstant(&chunk, 3.4);
	writeChunk(&chunk, OP_CONSTANT, 123);
	writeChunk(&chunk, constant, 123);

	writeChunk(&chunk, OP_ADD, 123);

	constant = addConstant(&chunk, 5.6);
	writeChunk(&chunk, OP_CONSTANT, 123);
	writeChunk(&chunk, constant, 123);

	writeChunk(&chunk, OP_DIVIDE, 123);

	writeChunk(&chunk, OP_NEGATE, 123);
	writeChunk(&chunk, OP_RETURN, 123);
	disassembleChunk(&chunk, "test chunk");
	interpret(&chunk);
	//freeVM();
	freeChunk(&chunk);
	
	print("hello\n");
	return 0;
	
}
