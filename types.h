#ifndef lux_types_h
#define lux_types_h

/* types.h - canonical type definitions for Plan9 C
 * This file contains ONLY type definitions and forward declarations.
 * No #includes, no macros, no function declarations.
 * Every .c file includes this FIRST to ensure identical type signatures.
 */

/* Forward declarations */
typedef struct VM VM;
typedef struct Obj Obj;
typedef struct ObjString ObjString;
typedef struct Chunk Chunk;
typedef struct Value Value;
typedef struct ValueArray ValueArray;

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
    OP_EQUAL,
    OP_GREATER,
    OP_LESS,
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_NOT,
    OP_NEGATE,
    OP_RETURN
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
#define STACK_MAX 256

struct VM {
    Chunk* chunk;
    uchar* ip;
    Value stack[STACK_MAX];
    Value* stackTop;
	Table strings;
    Obj* objects;
};

/* Object types */
typedef enum {
    OBJ_STRING
} ObjType;

struct Obj {
    ObjType type;
    struct Obj* next;
};

struct ObjString {
    Obj obj;
    int length;
    char* chars;
	unsigned long hash;
};

#endif

