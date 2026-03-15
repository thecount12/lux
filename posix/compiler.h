#ifndef lux_compiler_h
#define lux_compiler_h

#include "object.h"
#include "vm.h"

typedef struct {
	bool lint;
	int warningCount;
} CompileOptions;

ObjFunction* compile(const char* source);
ObjFunction* compileWithOptions(const char* source, CompileOptions* opts);
void markCompilerRoots();

#endif
