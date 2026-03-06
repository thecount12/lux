#include "lux.h"
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

/* Native function to read a file: readFile(path) -> string */
static Value 
readFileNative(int argCount, Value* args)
{
	int fd;
	long len;
	char *buf;
	Dir *d;
	long bytesRead;
	Value result;

	if (argCount != 1 || !IS_STRING(args[0]))
		return NIL_VAL;

	char* path = AS_CSTRING(args[0]);
	
	/* open() returns a file descriptor integer */
	fd = open(path, OREAD);
	if(fd < 0)
		return NIL_VAL;

	/* dirfstat is the Plan 9 way to get file metadata like length */
	d = dirfstat(fd);
	if(d == nil) {
		close(fd);
		return NIL_VAL;
	}
	len = d->length;
	free(d);

	buf = malloc(len + 1);
	if(buf == nil) {
		close(fd);
		return NIL_VAL;
	}

	bytesRead = read(fd, buf, len);
	if(bytesRead < 0) {
		free(buf);
		close(fd);
		return NIL_VAL;
	}
	
	buf[bytesRead] = '\0';
	close(fd);

	/* Wrap the raw C string into a Lux Value */
	result = OBJ_VAL(copyString(buf, (int)bytesRead));
	free(buf);
	return result;
}

/* Native function to write a file: writeFile(path, content) -> bool */
static Value 
writeFileNative(int argCount, Value* args)
{
	int fd;
	char* path;
	char* content;
	int contentLen;
	long bytesWritten;

	if (argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1]))
		return BOOL_VAL(false);

	path = AS_CSTRING(args[0]);
	content = AS_CSTRING(args[1]);
	contentLen = AS_STRING(args[1])->length;

	/* create() with OWRITE creates or truncates the file */
	fd = create(path, OWRITE, 0666);
	if(fd < 0)
		return BOOL_VAL(false);

	bytesWritten = write(fd, content, contentLen);
	close(fd);

	if(bytesWritten != contentLen)
		return BOOL_VAL(false);

	return BOOL_VAL(true);
}

/* Native function to append to a file: appendFile(path, content) -> bool */
static Value 
appendFileNative(int argCount, Value* args)
{
	int fd;
	char* path;
	char* content;
	int contentLen;
	long bytesWritten;

	if (argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1]))
		return BOOL_VAL(false);

	path = AS_CSTRING(args[0]);
	content = AS_CSTRING(args[1]);
	contentLen = AS_STRING(args[1])->length;

	/* open with OWRITE, file must exist */
	fd = open(path, OWRITE);
	if(fd < 0)
		return BOOL_VAL(false);

	/* seek to end of file */
	seek(fd, 0, 2);

	bytesWritten = write(fd, content, contentLen);
	close(fd);

	if(bytesWritten != contentLen)
		return BOOL_VAL(false);

	return BOOL_VAL(true);
}

/* Native function to delete a file: deleteFile(path) -> bool */
static Value 
deleteFileNative(int argCount, Value* args)
{
	if (argCount != 1 || !IS_STRING(args[0]))
		return BOOL_VAL(false);

	char* path = AS_CSTRING(args[0]);
	
	/* remove() works for files and empty directories */
	if(remove(path) < 0)
		return BOOL_VAL(false);

	return BOOL_VAL(true);
}

/* Native function to check if file exists: fileExists(path) -> bool */
static Value 
fileExistsNative(int argCount, Value* args)
{
	if (argCount != 1 || !IS_STRING(args[0]))
		return BOOL_VAL(false);

	char* path = AS_CSTRING(args[0]);
	
	/* access() with AEXIST checks if file exists */
	if(access(path, AEXIST) == 0)
		return BOOL_VAL(true);
	
	return BOOL_VAL(false);
}

/* Native function to create a directory: createDir(path) -> bool */
static Value 
createDirNative(int argCount, Value* args)
{
	int fd;

	if (argCount != 1 || !IS_STRING(args[0]))
		return BOOL_VAL(false);

	char* path = AS_CSTRING(args[0]);
	
	/* create with DMDIR flag creates a directory */
	fd = create(path, OREAD, DMDIR | 0775);
	if(fd < 0)
		return BOOL_VAL(false);

	close(fd);
	return BOOL_VAL(true);
}

/* Native function to list directory contents: listDir(path) -> string */
static Value 
listDirNative(int argCount, Value* args)
{
	int fd, n, i;
	Dir *dirs;
	char* path;
	char* result;
	int resultLen, resultCap;
	Value retval;

	if (argCount != 1 || !IS_STRING(args[0]))
		return NIL_VAL;

	path = AS_CSTRING(args[0]);
	
	/* open directory for reading */
	fd = open(path, OREAD);
	if(fd < 0)
		return NIL_VAL;

	/* allocate buffer for result string */
	resultCap = 1024;
	result = malloc(resultCap);
	if(result == nil) {
		close(fd);
		return NIL_VAL;
	}
	resultLen = 0;
	result[0] = '\0';

	/* read directory entries */
	while((n = dirread(fd, &dirs)) > 0) {
		for(i = 0; i < n; i++) {
			int nameLen = strlen(dirs[i].name);
			
			/* ensure buffer is large enough */
			while(resultLen + nameLen + 2 > resultCap) {
				resultCap *= 2;
				char* newResult = realloc(result, resultCap);
				if(newResult == nil) {
					free(dirs);
					free(result);
					close(fd);
					return NIL_VAL;
				}
				result = newResult;
			}
			
			/* append name and newline */
			strcpy(result + resultLen, dirs[i].name);
			resultLen += nameLen;
			result[resultLen++] = '\n';
			result[resultLen] = '\0';
		}
		free(dirs);
	}

	close(fd);

	/* remove trailing newline if present */
	if(resultLen > 0 && result[resultLen-1] == '\n') {
		result[resultLen-1] = '\0';
		resultLen--;
	}

	retval = OBJ_VAL(copyString(result, resultLen));
	free(result);
	return retval;
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

	vm.initString = nil;
	vm.initString = copyString("init", 4);

	defineNative("clock", clockNative);
	defineNative("readFile", readFileNative);
	defineNative("writeFile", writeFileNative);
	defineNative("appendFile", appendFileNative);
	defineNative("deleteFile", deleteFileNative);
	defineNative("fileExists", fileExistsNative);
	defineNative("createDir", createDirNative);
	defineNative("listDir", listDirNative);
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
			case OBJ_BOUND_METHOD: {
				ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
				vm.stackTop[-argCount -1] = bound->receiver;
				return call(bound->method, argCount);
			}
			case OBJ_CLASS: {
				ObjClass* klass = AS_CLASS(callee);
				vm.stackTop[-argCount -1] = OBJ_VAL(newInstance(klass));
				Value initializer;
				if (tableGet(&klass->methods, vm.initString, &initializer)) {
					return call(AS_CLOSURE(initializer), argCount);
				}
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

static bool 
invokeFromClass(ObjClass* klass, ObjString* name, int argCount)
{
	Value method;
	if (!tableGet(&klass->methods, name, &method)) {
		runtimeError("Undefined property '%s'.", name->chars);
		return false;
	}
	return call(AS_CLOSURE(method), argCount);
}

static bool 
invoke(ObjString* name, int argCount)
{
	Value receiver = peek(argCount);

	if (!IS_INSTANCE(receiver)) {
		runtimeError("Only instances have methods.");
		return false;
	}

	ObjInstance* instance = AS_INSTANCE(receiver);

	Value value;
	if (tableGet(&instance->fields, name, &value)) {
		vm.stackTop[-argCount -1] = value;
		return callValue(value, argCount);
	}


	return invokeFromClass(instance->klass, name, argCount);
}

static bool 
bindMethod(ObjClass* klass, ObjString* name)
{
	Value method;
	if (!tableGet(&klass->methods, name, &method)) {
		runtimeError("Undefined property '%s'.", name->chars);
		return false;
	}
	
	ObjBoundMethod* bound = newBoundMethod(peek(0), AS_CLOSURE(method));
	pop();
	push(OBJ_VAL(bound));
	return true;
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

static void 
defineMethod(ObjString* name)
{
	Value method = peek(0);
	ObjClass* klass = AS_CLASS(peek(1));
	tableSet(&klass->methods, name, method);
	pop();
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

			if (!bindMethod(instance->klass, name)) {
					return INTERPRET_RUNTIME_ERROR;
				}
				break;
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

		case OP_GET_SUPER: {
			ObjString* name = READ_STRING();
			ObjClass* superclass = AS_CLASS(pop());

			if (!bindMethod(superclass, name)) {
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
			if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
				/* Both are numbers, add them numerically */
				b = pop();
				a = pop();
				push(NUMBER_VAL(AS_NUMBER(a) + AS_NUMBER(b)));
			} else {
				/* At least one is not a number, convert both to strings and concatenate */
				b = pop();
				a = pop();
				ObjString* bStr = valueToString(b);
				ObjString* aStr = valueToString(a);
				
				int length = aStr->length + bStr->length;
				char* chars = ALLOCATE(char, length + 1);
				memcpy(chars, aStr->chars, aStr->length);
				memcpy(chars + aStr->length, bStr->chars, bStr->length);
				chars[length] = '\0';
				
				ObjString* result = takeString(chars, length);
				push(OBJ_VAL(result));
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
		case OP_INVOKE: {
			ObjString* method = READ_STRING();
			int argCount = READ_BYTE();
			if (!invoke(method, argCount)) {
				return INTERPRET_RUNTIME_ERROR;
			}
			frame = &vm.frames[vm.frameCount -1];
			break;
		}
		case OP_SUPER_INVOKE: {
			ObjString* method = READ_STRING();
			int argCount = READ_BYTE();
			ObjClass* superclass = AS_CLASS(pop());
			if (!invokeFromClass(superclass, method, argCount)) {
				return INTERPRET_RUNTIME_ERROR;
			}
			frame = &vm.frames[vm.frameCount -1];
			break;
		}

		case OP_CLOSURE: {
			ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
			ObjClosure* closure = newClosure(function);
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
		case OP_INHERIT: {
			Value superclass = peek(1);
			if (!IS_CLASS(superclass)) {
				runtimeError("Superclasss must be a class.");
				return INTERPRET_RUNTIME_ERROR;
			}

			ObjClass* subclass = AS_CLASS(peek(0));
			tableAddAll(&AS_CLASS(superclass)->methods, &subclass->methods);
			pop(); // Subclass.
			break;
		}
		case OP_METHOD:
			defineMethod(READ_STRING());
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
