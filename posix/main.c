#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "common.h"
//#include "chunk.h"
//#include "debug.h"
#include "vm.h"
#include "../version.h"
#include "compiler.h"

static void repl() {
	char line[1024];	
	for (;;) {
		printf("> ");
		
		if (!fgets(line, sizeof(line), stdin)) {
			printf("\n");
			break;
		}
	interpret(line);
	}
}

static char* readFile(const char* path) {
	FILE* file = fopen(path, "rb");
	if (file == NULL) {
		fprintf(stderr, "Could not open file \"%s\" (%s).\n", path, strerror(errno));
		exit(74);
	}
	
	fseek(file, 0L, SEEK_END);
	size_t fileSize = ftell(file);
	rewind(file);

	char* buffer = (char*)malloc(fileSize + 1);
	if (buffer == NULL) {
		fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
		exit(74);
	}

	size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
	if (bytesRead < fileSize) {
		fprintf(stderr, "Could not read file \"%s\".\n", path);
		exit(74);
	}

	buffer[bytesRead] = '\0';

	fclose(file);
	return buffer;
}

static void runFile(const char* path, int scriptArgc, char** scriptArgv) {
	char* source = readFile(path);
	setScriptArgs(scriptArgc, scriptArgv);
	setCompileSourceName(path);
	InterpretResult result = interpret(source);
	setCompileSourceName(NULL);
	free(source);

	if (result == INTERPRET_COMPILE_ERROR) exit(65);
	if (result == INTERPRET_RUNTIME_ERROR) exit(70);
}


static void printVersion(void) {
	printf("lux %s %s/%s %s %s",
		LUX_VERSION, LUX_OS, LUX_ARCH, LUX_BRANCH, LUX_GIT);
#ifdef DB_SQLITE
	printf(" +sqlite");
#endif
#ifdef DB_POSTGRES
	printf(" +postgres");
#endif
#ifdef DB_MYSQL
	printf(" +mysql");
#endif
#ifdef DB_ORACLE
	printf(" +oracle");
#endif
	printf("\n");
}

int main(int argc, const char* argv[]) {
	if (argc >= 2 && (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0)) {
		if (argc != 2) {
			fprintf(stderr, "Usage: lux [-v] [script] [args...] | lux -c \"code\"\n");
			exit(64);
		}
		printVersion();
		return 0;
	}

	initVM();
	if (argc == 1) {
		setScriptArgs(0, NULL);
		repl();
	} else if (strcmp(argv[1], "-c") == 0) {
		if (argc != 3) {
			fprintf(stderr, "Usage: lux [-v] [script] [args...] | lux -c \"code\"\n");
			exit(64);
		}
		setScriptArgs(0, NULL);
		InterpretResult result = interpret(argv[2]);
		if (result == INTERPRET_COMPILE_ERROR) exit(65);
		if (result == INTERPRET_RUNTIME_ERROR) exit(70);
	} else if (argc >= 2) {
		runFile(argv[1], argc - 2, (char**)(argv + 2));
	} else {
		fprintf(stderr, "Usage: lux [-v] [script] [args...] | lux -c \"code\"\n");
		exit(64);
	}
	
	freeVM();
	return 0;
}
