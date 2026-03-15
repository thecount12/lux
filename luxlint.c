/*
 * luxlint - static checks for Lux: unused vars, unreachable code, duplicate functions, shadowed globals
 * Plan 9 build: luxlint.rc
 */
#include "lux.h"
#include "common.h"
#include "compiler.h"

void initVM(void);
void freeVM(void);

static char*
readFile(char* path)
{
	int fd;
	long len;
	char *buf;
	Dir *d;

	fd = open(path, OREAD);
	if(fd < 0) {
		fprint(2, "Could not open file \"%s\"\n", path);
		exits("open");
	}

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

void
main(int argc, char *argv[])
{
	char *source;
	ObjFunction *function;
	CompileOptions opts;

	if (argc != 2) {
		fprint(2, "Usage: luxlint <path>\n");
		exits("usage");
	}

	initVM();

	opts.lint = 1;
	opts.warningCount = 0;

	source = readFile(argv[1]);
	function = compileWithOptions(source, &opts);
	free(source);

	freeVM();

	if (function == nil) {
		exits("compile");
	}
	if (opts.warningCount > 0) {
		exits("warn");
	}
	exits(nil);
}
