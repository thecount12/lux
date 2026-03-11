#ifndef lux_vm_h
#define lux_vm_h

#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef struct {
	ObjClosure* closure;
	uint8_t* ip;
	Value* slots;
} CallFrame;

typedef struct {
	CallFrame frames[FRAMES_MAX];
	int frameCount;

	Value stack[STACK_MAX];
	Value* stackTop;
	Table globals;
	Table strings;
	Table imports;
	ObjString* initString;
	ObjUpvalue* openUpvalues;

	size_t bytesAllocated;
	size_t nextGC;
	Obj* objects;
	int grayCount;
	int grayCapacity;
	Obj** grayStack;

	bool nativePanic;
	char nativePanicMsg[256];
} VM;

typedef enum {
	INTERPRET_OK,
	INTERPRET_COMPILE_ERROR,
	INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm;

void initVM();
void freeVM();
//InterpretResult interpret(Chunk* chunk);
InterpretResult interpret(const char* source);
void push(Value value);
Value pop();

#endif