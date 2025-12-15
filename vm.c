#include "lux.h"
#include "vm.h"
#include "value.h"

void printValue(Value value);
int disassembleInstruction(Chunk* chunk, int offset); 

VM vm;

void initVM(void) {
	print(".");
}

void freeVM() {
	print(".");
}


// This function now contains all the loop logic
InterpretResult interpret(Chunk* chunk) 
{
	vm.chunk = chunk;
	vm.ip = vm.chunk->code;

    // Define the macros locally for the interpreter loop
    #define READ_BYTE() (*vm.ip++)
    #define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])

	for (;;) {
	disassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code));
     
		uchar instruction; 
		switch (instruction = READ_BYTE()) {
			case OP_CONSTANT: {
				Value constant = READ_CONSTANT();
				printValue(constant);
				print("\n");
				break;
			}
			case OP_RETURN: {
				// return INTERPRET_OK; // Function returns here
				// print("\n");
                #undef READ_BYTE
                #undef READ_CONSTANT
                return INTERPRET_OK;
			}
		}
	}
}
