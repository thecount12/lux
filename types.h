#ifndef lux_types_h
#define lux_types_h

/* types.h - canonical type definitions for Plan9 C
 * This file contains ONLY type definitions and forward declarations.
 * No #includes, no macros, no function declarations.
 * Every .c file includes this FIRST to ensure identical type signatures.
 */

#define UINT8_MAX 255
#define UINT8_COUNT (UINT8_MAX + 1)

/* Plan 9 bool type */
#ifndef lux_bool_defined
#define lux_bool_defined
typedef int bool;
#define true 1
#define false 0
#endif


/* Forward declarations */
typedef struct VM VM;
typedef struct Obj Obj;
typedef struct ObjString ObjString;
typedef struct ObjUpvalue ObjUpvalue;
typedef struct ObjClosure ObjClosure;
typedef struct ObjBoundMethod ObjBoundMethod;
typedef struct ObjClass ObjClass;
typedef struct ObjInstance ObjInstance;
typedef struct Chunk Chunk;
typedef struct Value Value;
typedef struct ValueArray ValueArray;
typedef struct Compiler Compiler;

/* Value type - MUST be identical in all compilation units */
typedef enum {
    VAL_BOOL,
    VAL_NIL,
    VAL_NUMBER,
    VAL_OBJ
} ValueType;

struct Value {
    ValueType type;
    union {
        int boolean;
        double number;
        Obj* obj;
    } as;
};

typedef struct {
	ObjString* key;
	Value value;
} Entry;

typedef struct {
	int count;
	int capacity;
	Entry* entries;
} Table;

/* ValueArray type - MUST be identical in all compilation units */
struct ValueArray {
    int capacity;
    int count;
    Value* values;
};

/* OpCode enum used by Chunk */
typedef enum {
    OP_CONSTANT,
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
	OP_POP,
	OP_GET_LOCAL,
	OP_SET_LOCAL,
	OP_GET_GLOBAL,
	OP_DEFINE_GLOBAL,
	OP_SET_GLOBAL,
	OP_GET_UPVALUE,
	OP_SET_UPVALUE,
	OP_GET_PROPERTY,
	OP_SET_PROPERTY,
    OP_EQUAL,
    OP_GREATER,
    OP_LESS,
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_NOT,
    OP_NEGATE,
	OP_PRINT,
	OP_JUMP,
	OP_JUMP_IF_FALSE,
	OP_LOOP,
	OP_CALL,
	OP_INVOKE,
	OP_CLOSURE,
	OP_CLOSE_UPVALUE,
    OP_RETURN,
	OP_CLASS,
	OP_METHOD
} OpCode;

/* Chunk struct */
struct Chunk {
    int count;
    int capacity;
    uchar* code;
    int* lines;
    ValueArray constants;
};

/* VM struct */
#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)


/* Forward declare ObjFunction before CallFrame uses it */
typedef struct ObjFunction ObjFunction;
typedef struct ObjClosure ObjClosure;


typedef struct {
	struct ObjClosure* closure;
	unsigned char* ip;
	Value* slots;
} CallFrame;

struct VM {
	CallFrame frames[FRAMES_MAX];
	int frameCount;

    Value stack[STACK_MAX];
    Value* stackTop;
	Table globals;
	Table strings;
	ObjString* initString;
	ObjUpvalue* openUpvalues;

	unsigned long bytesAllocated;
	unsigned long nextGC;
    Obj* objects;
	int grayCount;
	int grayCapacity;
	Obj** grayStack;
};

/* Object types */
typedef enum {
	OBJ_BOUND_METHOD,
	OBJ_CLASS,
	OBJ_CLOSURE,
	OBJ_FUNCTION,
	OBJ_INSTANCE,
	OBJ_NATIVE,
    OBJ_STRING,
	OBJ_UPVALUE,
} ObjType;

struct Obj {
    ObjType type;
	bool isMarked;
    struct Obj* next;
};

struct ObjFunction {
	Obj obj;
	int arity;
	int upvalueCount;
	Chunk chunk;
	ObjString* name;
};

typedef Value (*NativeFn)(int argCount, Value* args);

typedef struct ObjNative ObjNative;

struct ObjNative{
	Obj obj;
	NativeFn function;
};

struct ObjString {
    Obj obj;
    int length;
    char* chars;
	unsigned long hash;
};

struct ObjUpvalue {
	Obj obj;
	Value* location;
	Value closed;
	struct ObjUpvalue* next;
};

struct ObjClosure {
	Obj obj;
	ObjFunction* function;
	ObjUpvalue** upvalues;
	int upvalueCount;
};

struct ObjClass {
	Obj obj;
	ObjString* name;
	Table methods;
};

struct ObjInstance {
	Obj obj;
	ObjClass* klass;
	Table fields;
};
 
struct ObjBoundMethod {
	Obj obj;
	Value receiver;
	ObjClosure* method;
};

#endif

