#include "lux.h"
#include "common.h"
#include "value.h"
#include "chunk.h"
#include "vm.h"
#include "memory.h"
#include "object.h"
#include "table.h"

void initVM(void);
void freeVM(void);
void setScriptArgs(int argc, char** argv);

static void 
repl(void) 
{
	char line[1024];	
	int n;

	for (;;) {
		print("> ");
		/* read(0, ...) is the Plan 9 equivalent to fgets from stdin */
		n = read(0, line, sizeof(line) - 1);
		if (n <= 0) {
			print("\n");
			break;
		}
		line[n] = '\0';
		interpret(line);
	}
}

static char* 
readFile(char* path) 
{
	int fd;
	long len;
	char *buf;
	Dir *d;

	/* open() returns a file descriptor integer */
	fd = open(path, OREAD);
	if(fd < 0) {
		fprint(2, "Could not open file \"%s\"\n", path);
		exits("open");
	}

	/* dirfstat is the Plan 9 way to get file metadata like length */
	d = dirfstat(fd);
	if(d == nil) {
		close(fd);
		exits("stat");
	}
	len = d->length;
	free(d);

	buf = malloc(len + 1);
	if(buf == nil) {
		fprint(2, "Not enough memory to read \"%s\"\n", path);
		close(fd);
		exits("mem");
	}

	if(read(fd, buf, len) != len) {
		fprint(2, "Could not read file \"%s\"\n", path);
		free(buf);
		close(fd);
		exits("read");
	}

	buf[len] = '\0';
	close(fd);
	return buf;
}

static void 
runFile(char* path, int scriptArgc, char** scriptArgv) 
{
	char* source = readFile(path);
	setScriptArgs(scriptArgc, scriptArgv);
	InterpretResult result = interpret(source);
	free(source);

	if (result == INTERPRET_COMPILE_ERROR) exits("compile error");
	if (result == INTERPRET_RUNTIME_ERROR) exits("runtime error");
}


void initVM(void);

void
main(int argc, char *argv[])
{
	initVM();

	if (argc == 1) {
		setScriptArgs(0, nil);
		repl();
	} else if (strcmp(argv[1], "-c") == 0) {
		if (argc != 3) {
			fprint(2, "Usage: lux [path] [args...] | lux -c \"code\"\n");
			exits("usage");
		}
		setScriptArgs(0, nil);
		InterpretResult result = interpret(argv[2]);
		if (result == INTERPRET_COMPILE_ERROR) exits("compile error");
		if (result == INTERPRET_RUNTIME_ERROR) exits("runtime error");
	} else if (argc >= 2) {
		runFile(argv[1], argc - 2, argv + 2);
	} else {
		fprint(2, "Usage: lux [path] [args...] | lux -c \"code\"\n");
		exits("usage");
	}
	
	freeVM();
	exits(nil);
}
