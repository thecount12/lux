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

static int 
invokeInstruction(const char* name, Chunk* chunk, int offset)
{
	unsigned char constant = chunk->code[offset +1 ];
	unsigned char argCount = chunk->code[offset +2 ];
	print("%-16s (%d args) %4d '", name, argCount, constant);
	printValue(chunk->constants.values[constant]);
	print("'\n");
	return offset + 3;
}

static int 
simpleInstruction(char* name, int offset)
{
    print("%s\n", name);
    return offset + 1;
}

static int 
byteInstruction(const char* name, Chunk* chunk, int offset)
{
	unsigned char slot = chunk->code[offset +1];
	print("%-16s %4d\n", name, (int)slot);
	return offset + 2;
}

static int
jumpInstruction(char* name, int sign, Chunk* chunk, int offset)
{
	unsigned char jump = (uchar)(chunk->code[offset +1] << 8);
	jump |= chunk->code[offset + 2];
	print("%-16s %4d -> %d\n", name, offset, offset + 3 + sign * jump);
	return offset + 3;
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
		case OP_GET_LOCAL:
			return byteInstruction("OP_GET_LOCAL", chunk, offset);
		case OP_SET_LOCAL:
			return byteInstruction("OP_SET_LOCAL", chunk, offset);
		case OP_GET_GLOBAL:
			return constantInstruction("OP_GET_GLOBAL", chunk, offset);
		case OP_DEFINE_GLOBAL:
			return constantInstruction("OP_DEFINE_GLOBAL", chunk, offset);
		case OP_SET_GLOBAL:
			return constantInstruction("OP_SET_GLOBAL", chunk, offset);
		case OP_GET_UPVALUE:
			return byteInstruction("OP_GET_UPVALUE", chunk, offset);
		case OP_SET_UPVALUE:
			return byteInstruction("OP_SET_UPVALUE", chunk, offset);
		case OP_GET_PROPERTY:
			return constantInstruction("OP_GET_PROPERTY", chunk, offset);
		case OP_SET_PROPERTY:
			return constantInstruction("OP_SET_PROPERTY", chunk, offset);
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
		case OP_JUMP:
			return jumpInstruction("OP_JUMP", 1, chunk, offset);
		case OP_JUMP_IF_FALSE:
			return jumpInstruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
		case OP_LOOP:
			return jumpInstruction("OP_LOOP", -1, chunk, offset);
		case OP_CALL:
			return byteInstruction("OP_CALL", chunk, offset);
		case OP_INVOKE:
			return invokeInstruction("OP_INVOKE", chunk, offset);
		case OP_CLOSURE: {
			offset++;
			unsigned constant = chunk->code[offset++];
			print("%-16s %4d ", "OP_CLOSURE", constant);
			printValue(chunk->constants.values[constant]);
			print("\n");

			ObjFunction* function = AS_FUNCTION(
				chunk->constants.values[constant]);
			for (int j = 0; j < function->upvalueCount; j++) {
				int isLocal = chunk->code[offset++];
				int index = chunk->code[offset++];
				print("%04d      |                     %s %d\n",
					offset - 2, isLocal ? "local" : "upvalue", index);
			}

			return offset;
		}
		case OP_CLOSE_UPVALUE:
			return simpleInstruction("OP_CLOSE_UPVALUE", offset);
        case OP_RETURN:
            return simpleInstruction("OP_RETURN", offset);
		case OP_CLASS:
			return constantInstruction("OP_CLASS", chunk, offset);
		case OP_METHOD:
			return constantInstruction("OP_METHOD", chunk, offset);
        default:
            print("Unknown opcode %d\n", instruction);
            return offset + 1;
    }
}
