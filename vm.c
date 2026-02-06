#include "lux.h"
#include "types.h"
#include "common.h"
#include "value.h"
#include "chunk.h"
#include "vm.h"
#include "memory.h"
#include "object.h"
#include "compiler.h"
#include "debug.h"
#include "table.h"

VM vm;

static void 
resetstack(void)
{
	vm.stackTop = vm.stack;
}

static void 
runtimeError(char *format, ...)
{
	va_list args;
	long inst;
	int line;

	va_start(args, format);
	/* Plan 9 uses fd 2 for stderr; fprint is the native print to fd */
	vfprint(2, format, args);
	va_end(args);
	fprint(2, "\n");

	/* Pointer subtraction returns long/vlong; size_t is not a Plan 9 type */
	inst = vm.ip - vm.chunk->code - 1;
	line = vm.chunk->lines[inst];
	fprint(2, "[line %d] in script\n", line);
	resetstack();
}

void 
initVM(void)
{
	resetstack();
	vm.objects = nil;
	initTable(&vm.globals);
	initTable(&vm.strings);
}

void 
freeVM(void)
{
	freeTable(&vm.globals);
	freeTable(&vm.strings);
	freeObjects();
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

static Value 
peek(int distance)
{
	return vm.stackTop[-1 - distance];
}

static int 
isFalsey(Value value)
{
	return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void
concatenate(void)
{
	Value b_val = pop();
	ObjString* b = AS_STRING(b_val);
	Value a_val = pop();
	ObjString* a = AS_STRING(a_val);
	int length = a->length + b->length;
	char* chars = ALLOCATE(char, length + 1);
	memcpy(chars, a->chars, a->length);
	memcpy(chars + a->length, b->chars, b->length);
	chars[length] = '\0';

	ObjString* result = takeString(chars, length);
	push(OBJ_VAL(result));
}
static InterpretResult 
run(void)
{
	uchar instruction;
	Value a, b, constant;
	double da, db;
	Value *slot;

	#define READ_BYTE() (*vm.ip++)
	#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
	#define READ_SHORT() \
		(vm.ip += 2, (unsigned int)((vm.ip[-2] << 8) | vm.ip[-1])) // uint16_t in posix linux
	#define READ_STRING() AS_STRING(READ_CONSTANT())
	
	for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
		/* Note: 'slot' is already declared at top of function */
		print("        ");
		for (slot = vm.stack; slot < vm.stackTop; slot++) {
			print("[ ");
			printValue(*slot);
			print(" ]");
		}
		print("\n");
		disassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code));
#endif
		instruction = READ_BYTE();
		USED(instruction); /* Silences 'set and not used' warning */

		switch (instruction) {
		case OP_CONSTANT:
			constant = READ_CONSTANT();
			push(constant);
			break;

		case OP_NIL:   push(NIL_VAL); break;
		case OP_TRUE:  push(BOOL_VAL(1)); break;
		case OP_FALSE: push(BOOL_VAL(0)); break;
		case OP_POP: pop(); break;
		case OP_GET_LOCAL: {
			int slot = READ_BYTE();
			push(vm.stack[slot]);
			break;
		}
		case OP_SET_LOCAL: {
			int slot = READ_BYTE();
			vm.stack[slot] = peek(0);
			break;
		}
		case OP_GET_GLOBAL: {
			ObjString* name = READ_STRING();
			Value value;
			if (!tableGet(&vm.globals, name, &value)) {
				runtimeError("Undefined variable '%s'.", name->chars);
				return INTERPRET_RUNTIME_ERROR;
			}
			push(value);
			break;
		}
		case OP_DEFINE_GLOBAL: {
			ObjString* name = READ_STRING();
			tableSet(&vm.globals, name, peek(0));
			pop();
			break;
		}

		case OP_SET_GLOBAL: {
			ObjString* name = READ_STRING();
			if (tableSet(&vm.globals, name, peek(0))) {
				tableDelete(&vm.globals, name);
				runtimeError("Undefined variable '%s'.", name->chars);
				return INTERPRET_RUNTIME_ERROR;
			}
			break;	
		}

		case OP_EQUAL:
			b = pop();
			a = pop();
			push(BOOL_VAL(valuesEqual(a, b)));
			break;

		case OP_GREATER:
			b = pop(); a = pop();
			if (!IS_NUMBER(a) || !IS_NUMBER(b)) {
				runtimeError("Operands must be numbers.");
				return INTERPRET_RUNTIME_ERROR;
			}
			push(BOOL_VAL(AS_NUMBER(a) > AS_NUMBER(b)));
			break;

		case OP_LESS:
			b = pop(); a = pop();
			if (!IS_NUMBER(a) || !IS_NUMBER(b)) {
				runtimeError("Operands must be numbers.");
				return INTERPRET_RUNTIME_ERROR;
			}
			push(BOOL_VAL(AS_NUMBER(a) < AS_NUMBER(b)));
			break;

		case OP_ADD:
			b = pop();
			a = pop();
			if (!IS_NUMBER(a) || !IS_NUMBER(b)) {
				runtimeError("Operands must be numbers.");
				return INTERPRET_RUNTIME_ERROR;
			}
			push(NUMBER_VAL(AS_NUMBER(a) + AS_NUMBER(b)));
			break;	

		case OP_SUBTRACT:
			b = pop(); a = pop();
			if (!IS_NUMBER(a) || !IS_NUMBER(b)) {
				runtimeError("Operands must be numbers.");
				return INTERPRET_RUNTIME_ERROR;
			}
			da = AS_NUMBER(a);
			db = AS_NUMBER(b);
			push(NUMBER_VAL(da - db));
			break;

		case OP_MULTIPLY:
			b = pop(); a = pop();
			if (!IS_NUMBER(a) || !IS_NUMBER(b)) {
				runtimeError("Operands must be numbers.");
				return INTERPRET_RUNTIME_ERROR;
			}
			da = AS_NUMBER(a);
			db = AS_NUMBER(b);
			push(NUMBER_VAL(da * db));
			break;

		case OP_DIVIDE:
			b = pop(); a = pop();
			if (!IS_NUMBER(a) || !IS_NUMBER(b)) {
				runtimeError("Operands must be numbers.");
				return INTERPRET_RUNTIME_ERROR;
			}
			da = AS_NUMBER(a);
			db = AS_NUMBER(b);
			push(NUMBER_VAL(da / db));
			break;

		case OP_NOT:
			a = pop();
			push(BOOL_VAL(isFalsey(a)));
			break;

		case OP_NEGATE:
			if (!IS_NUMBER(peek(0))) {
				runtimeError("Operand must be a number.");
				return INTERPRET_RUNTIME_ERROR;
			}
			a = pop();
			push(NUMBER_VAL(-AS_NUMBER(a)));
			break;

		case OP_PRINT:
			a = pop();
			printValue(a);
			print("\n");
			break;

		case OP_JUMP: {
			unsigned char offset = READ_SHORT(); // uint16_t in posix linux
			vm.ip += offset;
			break;			
		}

		case OP_JUMP_IF_FALSE: {
			unsigned char offset = READ_SHORT();
			if (isFalsey(peek(0))) vm.ip += offset;
			break;
		}
		
		case OP_LOOP: {
			unsigned char offset = READ_SHORT();
			vm.ip -= offset;
			break;
		}

		case OP_RETURN:
			//constant = pop(); /* use 'constant' as a temporary Value */
			//printValue(constant);
			//print("\n");
			return INTERPRET_OK;
		}
	}

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
}

InterpretResult 
interpret(char *source)
{
	Chunk chunk;
	InterpretResult result;

	initChunk(&chunk);

	if (!compile(source, &chunk)) {
		freeChunk(&chunk);
		return INTERPRET_COMPILE_ERROR;
	}

	vm.chunk = &chunk;
	vm.ip = vm.chunk->code;

	result = run();

	freeChunk(&chunk);
	return result;
}
