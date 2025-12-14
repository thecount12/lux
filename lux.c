#include "lux.h"
#include "chunk.h"
#include "debug.h"
#include "vm.h"

void disassembleChunk(Chunk* chunk, const char* name);
// void initVM();
// void freeVM();
InterpretResult interpret(Chunk* chunk);

//int main(int argc, const char* argv[])
int main()
{
	//initVM();
	Chunk chunk;
	initChunk(&chunk);

	int constant = addConstant(&chunk, 1.2);
	writeChunk(&chunk, OP_CONSTANT, 123);
	writeChunk(&chunk, constant, 123);

	writeChunk(&chunk, OP_RETURN, 123);

	disassembleChunk(&chunk, "test chunk");
	interpret(&chunk);
	//freeVM();
	freeChunk(&chunk);
	print("hello\n");
	return 0;
}
