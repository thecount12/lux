#ifndef lux_vm_h
#define lux_vm_h

/* vm.h - function declarations only
 * Assumes: types.h has been included by the .c file
 */

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

void initVM(void);
void freeVM(void);

InterpretResult interpret(char* source);
void push(Value value);
Value pop(void);

extern VM vm;

#endif
