#include "lux.h"
#include "types.h"
#include "common.h"
#include "value.h"
#include "chunk.h"
#include "vm.h"
#include "memory.h"
#include "object.h"
#include "debug.h"

/* we will add a header so we can track this*/
void 
disassembleChunk(Chunk* chunk, char* name) 
{
    print("== %s == \n", name);
    for (int offset = 0; offset < chunk->count;) {
        offset = disassembleInstruction(chunk, offset);
    }
}

static
int constantInstruction(char* name, Chunk* chunk, int offset)
{
	int constant = chunk->code[offset +1];
	print("%-16s %4d '", name, constant);
	printValue(chunk->constants.values[constant]);
	print("'\n");
	return offset + 2;
}

static int simpleInstruction(char* name, int offset)
{
    print("%s\n", name);
    return offset + 1;
}


/* print byte from OP_RETURN */
int disassembleInstruction(Chunk* chunk, int offset) 
{
    print("%04d ", offset);
	if (offset > 0 &&
		chunk->lines[offset] == chunk->lines[offset -1]) {
		print("   | ");
	} else {
		print("%4d ", chunk->lines[offset]);
	}
    unsigned char instruction = chunk->code[offset];
    // this will grow overtime
    switch (instruction) {
		case OP_CONSTANT:
			return constantInstruction("OP_CONSTANT", chunk, offset);
		case OP_NIL:
			return simpleInstruction("OP_NIL", offset);
		case OP_TRUE:
			return simpleInstruction("OP_TRUE", offset);
		case OP_FALSE:
			return simpleInstruction("OP_FALSE", offset);
		case OP_POP:
			return simpleInstruction("OP_POP", offset);
		case OP_GET_GLOBAL:
			return constantInstruction("OP_GET_GLOBAL", chunk, offset);
		case OP_DEFINE_GLOBAL:
			return constantInstruction("OP_DEFINE_GLOBAL", chunk, offset);
		case OP_SET_GLOBAL:
			return constantInstruction("OP_SET_GLOBAL", chunk, offset);
		case OP_EQUAL:
			return simpleInstruction("OP_EQUAL", offset);
		case OP_GREATER:
			return simpleInstruction("OP_GREATER", offset);
		case OP_LESS:
			return simpleInstruction("OP_LESS", offset);
		case OP_ADD:
			return simpleInstruction("OP_ADD", offset);
		case OP_SUBTRACT:
			return simpleInstruction("OP_SUBTRACT", offset);
		case OP_MULTIPLY:
			return simpleInstruction("OP_MULTIPLY", offset);
		case OP_DIVIDE:
			return simpleInstruction("OP_DIVIDE", offset);
		case OP_NOT:
			return simpleInstruction("OP_NOT", offset);
		case OP_NEGATE:
			return simpleInstruction("OP_NEGATE", offset);
		case OP_PRINT:
			return simpleInstruction("OP_PRINT", offset);
        case OP_RETURN:
            return simpleInstruction("OP_RETURN", offset);
        default:
            print("Unknown opcode %d\n", instruction);
            return offset + 1;
    }
}
