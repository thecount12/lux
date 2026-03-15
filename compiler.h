#ifndef lux_compiler_h
#define lux_compiler_h

typedef struct {
	int lint;
	int warningCount;
} CompileOptions;

ObjFunction* compile(char* source);
ObjFunction* compileWithOptions(char* source, CompileOptions* opts);
void markCompilerRoots();

#endif
