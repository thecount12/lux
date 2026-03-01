#include "lux.h"
#include "types.h"
#include "common.h"
#include "value.h"
#include "chunk.h"
#include "object.h"
#include "vm.h"
#include "memory.h"
#include "compiler.h"
#include "debug.h"
#include "table.h"

VM vm;

/* posix linux only 
static Value 
clockNative(int argCount, Value* args)
{
	return NUMBER_VAL((double)clock() / CLOCK_PER_SEC);
}
*/

static Value 
clockNative(int argCount, Value* args)
{
    (void)argCount; (void)args;
    // Plan 9: use nsec() for nanoseconds since boot, divide by 1e9 for seconds
    return NUMBER_VAL((double)nsec() / 1000000000.0);
}


static void 
resetStack(void)
{
	vm.stackTop = vm.stack;
	vm.frameCount = 0;
	vm.openUpvalues = nil;
}

static void 
runtimeError(char *format, ...)
{
	va_list args;

	va_start(args, format);
	/* Plan 9 uses fd 2 for stderr; fprint is the native print to fd */
	vfprint(2, format, args);
	va_end(args);
	fprint(2, "\n");

	/* Pointer subtraction returns long/vlong; size_t is not a Plan 9 type */

	for (int i = vm.frameCount -1; i >= 0; i--) {
		CallFrame* frame = &vm.frames[i];
		ObjFunction* function = frame->closure->function;
		long instruction = frame->ip - function->chunk.code -1;
		print("[line %d] in ", function->chunk.lines[instruction]);
		if (function->name == nil) {
			print("script\n");
		} else {
			print("%s()\n", function->name->chars);
		}
	}

	resetStack();
}


static void 
defineNative(const char* name, NativeFn function)
{
	push(OBJ_VAL(copyString(name, (int)strlen(name))));
	push(OBJ_VAL(newNative(function)));
	tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
	pop();
	pop();
}


void 
initVM(void)
{
	resetStack();
	vm.objects = nil;

	vm.grayCount = 0;
	vm.grayCapacity = 0;
	vm.grayStack = nil;

	vm.bytesAllocated = 0;
	vm.nextGC = 1024 * 1024;


	initTable(&vm.globals);
	initTable(&vm.strings);

	defineNative("clock", clockNative);
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

static bool 
call(ObjClosure* closure, int argCount) 
{
	if (argCount != closure->function->arity) {
		runtimeError("Expected %d arguments but got %d.",
			closure->function->arity, argCount);
		return false;
	}
	if (vm.frameCount == FRAMES_MAX) {
		runtimeError("Stack overflow.");
		return false;
	}

	CallFrame* frame = &vm.frames[vm.frameCount++];
	frame->closure = closure;
	frame->ip = closure->function->chunk.code;
	frame->slots = vm.stackTop - argCount -1;
	return true;
}

static bool callValue(Value callee, int argCount) {
	if (IS_OBJ(callee)) {
		switch (OBJ_TYPE(callee)) {
			case OBJ_CLASS: {
				ObjClass* klass = AS_CLASS(callee);
				vm.stackTop[-argCount -1] = OBJ_VAL(newInstance(klass));
				return true;
			}
			case OBJ_CLOSURE:
				return call(AS_CLOSURE(callee), argCount);
			case OBJ_NATIVE: {
			    NativeFn native;
                Value result;
                native = AS_NATIVE(callee);
				result = native(argCount, vm.stackTop - argCount);
				vm.stackTop -= argCount + 1;
				push(result);
				return true;
			}
			default:
				break; // non callable object type
		}
	}
	runtimeError("Can only call functions and classes.");
	return false;
}

static ObjUpvalue* 
captureUpvalue(Value* local) 
{
	ObjUpvalue* prevUpvalue = nil;
	ObjUpvalue* upvalue = vm.openUpvalues;
	while (upvalue != nil && upvalue->location > local) {
		prevUpvalue = upvalue;
		upvalue = upvalue->next;
	}

	if (upvalue != nil && upvalue->location == local) {
		return upvalue;
	}

	ObjUpvalue* createdUpvalue = newUpvalue(local);
	createdUpvalue->next = upvalue;

	if (prevUpvalue == nil) {
		vm.openUpvalues = createdUpvalue;
	} else {
		prevUpvalue->next = createdUpvalue;
	}
	return createdUpvalue;
}

static void 
closeUpvalues(Value* last) 
{
	while (vm.openUpvalues != nil && vm.openUpvalues->location >= last) {
		ObjUpvalue* upvalue = vm.openUpvalues;
		upvalue->closed = *upvalue->location;
		upvalue->location = &upvalue->closed;
		vm.openUpvalues = upvalue->next;
	}
}

static int 
isFalsey(Value value)
{
	return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void
concatenate(void)
{
	Value b_val = peek(0);
	ObjString* b = AS_STRING(b_val);
	Value a_val = peek(1);
	ObjString* a = AS_STRING(a_val);
	int length = a->length + b->length;
	char* chars = ALLOCATE(char, length + 1);
	memcpy(chars, a->chars, a->length);
	memcpy(chars + a->length, b->chars, b->length);
	chars[length] = '\0';

	ObjString* result = takeString(chars, length);
	pop();
	pop();
	push(OBJ_VAL(result));
}
static InterpretResult 
run(void)
{
	CallFrame* frame = &vm.frames[vm.frameCount -1];
	uchar instruction;
	Value a, b, constant;
	double da, db;
	Value *slot;

#define READ_BYTE() (*frame->ip++)

#define READ_SHORT() \
	(frame->ip += 2, \
	(ushort)((frame->ip[-2] << 8) | frame->ip[-1]))

#define READ_CONSTANT() \
	(frame->closure->function->chunk.constants.values[READ_BYTE()])

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
		disassembleInstruction(&frame->closure->function->chunk, (int)(frame->ip - frame->closure->function->chunk.code));
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
			uchar slotnum;
			slotnum = READ_BYTE();
			push(frame->slots[slotnum]);
			break;
		}
		case OP_SET_LOCAL: {
			uchar slotnum;
			slotnum = READ_BYTE();
			frame->slots[slotnum] = peek(0);
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

		case OP_GET_UPVALUE: {
			unsigned slot = READ_BYTE();
			push(*frame->closure->upvalues[slot]->location);
			break;
		}

		case OP_SET_UPVALUE: {
			unsigned slot = READ_BYTE();
			*frame->closure->upvalues[slot]->location = peek(0);
			break;
		}

		case OP_GET_PROPERTY: {
			if (!IS_INSTANCE(peek(0))) {
				runtimeError("Only instances have properties.");
				return INTERPRET_RUNTIME_ERROR;
			}


			ObjInstance* instance = AS_INSTANCE(peek(0));
			ObjString* name = READ_STRING();

			Value value;
			if (tableGet(&instance->fields, name, &value)) {
				pop(); // Instance
				push(value);
				break;
			}
			runtimeError("Undefined property '%s'.", name->chars);
			return INTERPRET_RUNTIME_ERROR;
		}

		case OP_SET_PROPERTY: {
			if (!IS_INSTANCE(peek(1))) {
				runtimeError("Only instances have fields.");
				return INTERPRET_RUNTIME_ERROR;
			}

			ObjInstance* instance = AS_INSTANCE(peek(1));
			tableSet(&instance->fields, READ_STRING(), peek(0));
			Value value = pop();
			pop();
			push(value);
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
			if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
				concatenate();
			} else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
				b = pop();
				a = pop();
				push(NUMBER_VAL(AS_NUMBER(a) + AS_NUMBER(b)));
			} else {
				runtimeError("Operands must be two numbers or two strings.");
				return INTERPRET_RUNTIME_ERROR;
			}
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
			frame->ip += offset;
			break;			
		}

		case OP_JUMP_IF_FALSE: {
			unsigned char offset = READ_SHORT();
			if (isFalsey(peek(0))) frame->ip += offset;
			break;
		}
		
		case OP_LOOP: {
			unsigned char offset = READ_SHORT();
			frame->ip -= offset;
			break;
		}
			
		case OP_CALL: {
			int argCount = READ_BYTE();
			if (!callValue(peek(argCount), argCount)) {
				return INTERPRET_RUNTIME_ERROR;
			}
			frame = &vm.frames[vm.frameCount -1];
			break;
		}

		case OP_CLOSURE: {
			ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
			ObjClosure* closure = newClosure(function);
			push(OBJ_VAL(closure));
			push(OBJ_VAL(closure));
			for (int i = 0; i < closure->upvalueCount; i++) {
				unsigned isLocal = READ_BYTE();
				unsigned index = READ_BYTE();
				if (isLocal) {
					closure->upvalues[i] = captureUpvalue(frame->slots + index);
				} else {
					closure->upvalues[i] = frame->closure->upvalues[index];
				}
			}
			break;
		}
		case OP_CLOSE_UPVALUE:
			closeUpvalues(vm.stackTop - 1);
			pop();
			break;
		case OP_RETURN: {
			Value result;
			result = pop();
			closeUpvalues(frame->slots);
			vm.frameCount--;
			if (vm.frameCount == 0) {
				pop();
				return INTERPRET_OK;
			}
			
			vm.stackTop = frame->slots;
			push(result);
			frame = &vm.frames[vm.frameCount -1];
			break;
		}
		case OP_CLASS:
			push(OBJ_VAL(newClass(READ_STRING())));
			break;	
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
	InterpretResult result;
	ObjFunction* function = compile(source);
	if (function == nil) return INTERPRET_COMPILE_ERROR;

	push (OBJ_VAL(function));
	ObjClosure* closure = newClosure(function);
	pop();
	push(OBJ_VAL(closure));
	call(closure, 0);

	result = run();
	return result;
}
