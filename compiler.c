#include "lux.h"
#include "common.h"
#include "scanner.h"
#include "compiler.h"

void 
compile(char* source) 
{
	/* 1. Hoist ALL declarations to the absolute top of the block */
	int line;
	Token token;

	/* 2. NO executable code before these lines */
	line = -1;
	initScanner(source);

	for (;;) {
		token = scanToken();

		if (token.line != line) {
			print("%4d ", token.line);
			line = token.line;
		} else {
			print("   | ");
		}

		print("%2d '%.*s'\n", token.type, token.length, token.start);

		if (token.type == TOKEN_EOF) {
			break;
		}
	}
}
