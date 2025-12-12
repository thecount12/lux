#include "lux.h"
#include "chunk.h"

/* Manual Prototype added to satisfy the compiler */
void disassembleChunk(Chunk* chunk, const char* name);
/* End Manual Prototype */


//int main(int argc, const char* argv[])
int main()
{
	Chunk chunk;
	initChunk(&chunk);

	int constant = addConstant(&chunk, 1.2);
	writeChunk(&chunk, OP_CONSTANT, 123);
	writeChunk(&chunk, constant, 123);

	writeChunk(&chunk, OP_RETURN, 123);

	disassembleChunk(&chunk, "test chunk");
	freeChunk(&chunk);
	print("hello\n");
	return 0;
}
