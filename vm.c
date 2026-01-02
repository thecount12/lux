#include "lux.h"
#include "common.h"
#include "value.h"
#include "compiler.h"
#include "debug.h"
#include "vm.h"

void printValue(Value value);
int disassembleInstruction(Chunk* chunk, int offset); 

VM vm;

static void 
resetStack(void)
{
	vm.stackTop = vm.stack;
}

void 
push(Value value)
{
	*vm.stackTop = value;
	vm.stackTop++;
}

Value 
pop(void)
{
	vm.stackTop--;
	return *vm.stackTop;
}

static InterpretResult 
run(void) 
{
	#define READ_BYTE() (*vm.ip++)
	#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
	#define BINARY_OP(op) \
		do { \
			double b = pop(); \
			double a = pop(); \
			push(a op b); \
		} while (0)

	for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
		Value* slot;
		print("        ");
		for (slot = vm.stack; slot < vm.stackTop; slot++) {
			print("[ ");
			printValue(*slot);
			print(" ]");
		}
		print("\n");
		disassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code));
#endif
		uchar instruction; 
		instruction = READ_BYTE();
		
		/* Plan 9: tell compiler we know we set this but aren't using 
		   it for anything other than the switch control flow */
		USED(instruction);

		switch (instruction) {
			case OP_CONSTANT: {
				Value constant = READ_CONSTANT();
				push(constant);
				break;
			}
			case OP_ADD:        BINARY_OP(+); break;
			case OP_SUBTRACT:   BINARY_OP(-); break;
			case OP_MULTIPLY:   BINARY_OP(*); break;
			case OP_DIVIDE:     BINARY_OP(/); break;
			case OP_NEGATE:     push(-pop()); break;
			case OP_RETURN: {
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

extern int compile(char* source, Chunk* chunk);

InterpretResult 
interpret(char* source) 
{
	Chunk chunk;
	InterpretResult result; /* Declared ONCE at top */

	initChunk(&chunk);

	if (!compile(source, &chunk)) {
		freeChunk(&chunk);
		return INTERPRET_COMPILE_ERROR;
	}

	vm.chunk = &chunk;
	vm.ip = vm.chunk->code;

	/* result = run(); NO TYPE NAME HERE to avoid redeclaration */
	result = run();

	freeChunk(&chunk);
	return result;
}

void 
initVM(void)
{
	resetStack();
}

void 
freeVM(void)
{
	print(".");
}