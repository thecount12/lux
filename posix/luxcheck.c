#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "vm.h"

static char* readFile(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        exit(74);
    }
    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        fprintf(stderr, "Could not stat file \"%s\".\n", path);
        exit(74);
    }
    long fileSize = ftell(file);
    rewind(file);

    char* buffer = (char*)malloc((size_t)fileSize + 1);
    if (buffer == NULL) {
        fclose(file);
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        exit(74);
    }

    size_t bytesRead = fread(buffer, 1, (size_t)fileSize, file);
    if (bytesRead < (size_t)fileSize) {
        free(buffer);
        fclose(file);
        fprintf(stderr, "Could not read file \"%s\".\n", path);
        exit(74);
    }

    buffer[bytesRead] = '\0';
    fclose(file);
    return buffer;
}

int main(int argc, const char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: luxcheck <path>\n");
        return 64;
    }

    /* initVM is required because compiler/string interners use vm state */
    initVM();

    char* source = readFile(argv[1]);
    ObjFunction* function = compile(source);
    free(source);

    if (function == NULL) {
        freeVM();
        /* compilation failed */
        return 65;
    }

    freeVM();
    return 0;
}
