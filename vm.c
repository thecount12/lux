#include "lux.h"
#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "vm.h"
#include "value.h"

void printValue(Value value);
int disassembleInstruction(Chunk* chunk, int offset); 

VM vm;

/* 1. Helper functions must be defined at the top for visibility */
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

/* 2. The core execution loop - Defined BEFORE interpret() */
static InterpretResult 
run(void) 
{
    /* Macros are local to the run loop for safety */
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

/* 3. The public entry point */
InterpretResult 
interpret(char* source) 
{

    compile(source);
	return INTERPRET_OK;
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