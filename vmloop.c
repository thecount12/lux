#include "lux.h"
#include "common.h"
#include "debug.h"
#include "vm.h"
#include "value.h"

//static void resetStack(void);
//void push(Value value);
//Value pop();
void printValue(Value value);
int disassembleInstruction(Chunk* chunk, int offset); 

VM vm;

static void resetStack(void)
{
	vm.stackTop = vm.stack;
}

void initVM(void)
{
	//print(".");
	resetStack();
}

void freeVM(void)
{
	print(".");
}

void push(Value value)
{
	*vm.stackTop = value;
	vm.stackTop++;
}

Value pop(void)
{
	vm.stackTop--;
	return *vm.stackTop;
}


// This function now contains all the loop logic
InterpretResult interpret(Chunk* chunk) 
{
	vm.chunk = chunk;
	vm.ip = vm.chunk->code;

    // Define the macros locally for the interpreter loop
    #define READ_BYTE() (*vm.ip++)
    #define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
	#define BINARY_OP(op) \
		do { \
			double b = pop(); \
			double a = pop(); \
			push (a op b); \
		} while (0)
	// note: while(false) plan9 C does not use false
	for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
	print("        ");
	for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
		print("[ ");
		printValue(*slot);
		print(" ]");
	}
	print("\n");
	disassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code));
#endif
		uchar instruction; 
		switch (instruction = READ_BYTE()) {
			case OP_CONSTANT: {
				Value constant = READ_CONSTANT();
				//printValue(constant);
				push(constant);
				//print("\n");
				break;
			}
			case OP_ADD:		BINARY_OP(+); break;
			case OP_SUBTRACT:	BINARY_OP(-); break;
			case OP_MULTIPLY:	BINARY_OP(*); break;
			case OP_DIVIDE:		BINARY_OP(/); break;
			case OP_NEGATE: 	push(-pop()); break;
			case OP_RETURN: {
				// return INTERPRET_OK; // Function returns here
				printValue(pop());
				print("\n");
                #undef READ_BYTE
                #undef READ_CONSTANT
				#undef BINARY_OP
                return INTERPRET_OK;
			}
		}
	}
}
