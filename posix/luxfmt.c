#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} Buffer;

static void die(const char* msg) {
    fprintf(stderr, "luxfmt: %s\n", msg);
    exit(1);
}

static void initBuffer(Buffer* b) {
    b->cap = 4096;
    b->len = 0;
    b->data = (char*)malloc(b->cap);
    if (!b->data) die("out of memory");
    b->data[0] = '\0';
}

static void ensureCap(Buffer* b, size_t need) {
    if (need <= b->cap) return;
    while (b->cap < need) b->cap *= 2;
    char* grown = (char*)realloc(b->data, b->cap);
    if (!grown) die("out of memory");
    b->data = grown;
}

static void appendChar(Buffer* b, char c) {
    ensureCap(b, b->len + 2);
    b->data[b->len++] = c;
    b->data[b->len] = '\0';
}

static void appendStr(Buffer* b, const char* s) {
    size_t add = strlen(s);
    ensureCap(b, b->len + add + 1);
    memcpy(b->data + b->len, s, add);
    b->len += add;
    b->data[b->len] = '\0';
}

static char lastChar(const Buffer* b) {
    if (b->len == 0) return '\0';
    return b->data[b->len - 1];
}

static void trimTrailingSpaces(Buffer* b) {
    while (b->len > 0) {
        char c = b->data[b->len - 1];
        if (c == ' ' || c == '\t') {
            b->len--;
            b->data[b->len] = '\0';
        } else {
            break;
        }
    }
}

static void ensureNewline(Buffer* b) {
    trimTrailingSpaces(b);
    if (lastChar(b) != '\n') appendChar(b, '\n');
}

static void emitIndent(Buffer* out, int indent) {
    for (int i = 0; i < indent; i++) {
        appendStr(out, "    ");
    }
}

static char* readFile(const char* path, size_t* outLen) {
    FILE* f = fopen(path, "rb");
    if (!f) die("unable to open input file");

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        die("unable to seek input file");
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        die("unable to get input file size");
    }
    rewind(f);

    char* data = (char*)malloc((size_t)sz + 1);
    if (!data) {
        fclose(f);
        die("out of memory");
    }

    size_t n = fread(data, 1, (size_t)sz, f);
    fclose(f);
    if (n != (size_t)sz) {
        free(data);
        die("unable to read input file");
    }
    data[n] = '\0';
    *outLen = n;
    return data;
}

static void writeFile(const char* path, const char* data, size_t len) {
    FILE* f = fopen(path, "wb");
    if (!f) die("unable to open output file");
    size_t n = fwrite(data, 1, len, f);
    fclose(f);
    if (n != len) die("unable to write output file");
}

static const char* skipSpaces(const char* src, size_t len, size_t i) {
    while (i < len && isspace((unsigned char)src[i])) i++;
    return src + i;
}

static bool nextIsChar(const char* src, size_t len, size_t i, char needle) {
    const char* p = skipSpaces(src, len, i);
    return *p == needle;
}

static void emitWordBoundarySpace(Buffer* out, bool* atLineStart, bool* pendingSpace) {
    if (*pendingSpace && !*atLineStart) {
        char last = lastChar(out);
        if (last != ' ' && last != '\n' && last != '(' && last != '[' && last != '{') {
            appendChar(out, ' ');
        }
    }
    *pendingSpace = false;
}

static char* formatLux(const char* src, size_t len, size_t* outLen) {
    Buffer out;
    initBuffer(&out);

    bool inString = false;
    bool inLineComment = false;
    bool inBlockComment = false;
    bool escaped = false;
    bool atLineStart = true;
    bool pendingSpace = false;
    int indent = 0;

    for (size_t i = 0; i < len; i++) {
        char c = src[i];

        if (inString) {
            if (atLineStart) {
                emitIndent(&out, indent);
                atLineStart = false;
            }
            appendChar(&out, c);
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }

        if (inLineComment) {
            if (atLineStart) {
                emitIndent(&out, indent);
                atLineStart = false;
            }
            appendChar(&out, c);
            if (c == '\n') {
                inLineComment = false;
                atLineStart = true;
                pendingSpace = false;
            }
            continue;
        }

        if (inBlockComment) {
            if (atLineStart) {
                emitIndent(&out, indent);
                atLineStart = false;
            }
            appendChar(&out, c);
            if (c == '*' && i + 1 < len && src[i + 1] == '/') {
                appendChar(&out, '/');
                i++;
                inBlockComment = false;
            }
            if (c == '\n') atLineStart = true;
            continue;
        }

        if (isspace((unsigned char)c)) {
            if (c == '\n') {
                ensureNewline(&out);
                atLineStart = true;
                pendingSpace = false;
            } else {
                pendingSpace = true;
            }
            continue;
        }

        if (c == '/' && i + 1 < len && src[i + 1] == '/') {
            emitWordBoundarySpace(&out, &atLineStart, &pendingSpace);
            if (atLineStart) {
                emitIndent(&out, indent);
                atLineStart = false;
            }
            appendStr(&out, "//");
            i++;
            inLineComment = true;
            continue;
        }

        if (c == '/' && i + 1 < len && src[i + 1] == '*') {
            emitWordBoundarySpace(&out, &atLineStart, &pendingSpace);
            if (atLineStart) {
                emitIndent(&out, indent);
                atLineStart = false;
            }
            appendStr(&out, "/*");
            i++;
            inBlockComment = true;
            continue;
        }

        switch (c) {
            case '{': {
                trimTrailingSpaces(&out);
                if (!atLineStart && lastChar(&out) != ' ' && lastChar(&out) != '\n' && lastChar(&out) != '(') {
                    appendChar(&out, ' ');
                }
                if (atLineStart) emitIndent(&out, indent);
                appendChar(&out, '{');
                ensureNewline(&out);
                indent++;
                atLineStart = true;
                pendingSpace = false;
                break;
            }
            case '}': {
                if (indent > 0) indent--;
                ensureNewline(&out);
                emitIndent(&out, indent);
                appendChar(&out, '}');
                if (!nextIsChar(src, len, i + 1, ';')) {
                    ensureNewline(&out);
                    atLineStart = true;
                } else {
                    atLineStart = false;
                }
                pendingSpace = false;
                break;
            }
            case ';': {
                trimTrailingSpaces(&out);
                if (atLineStart) emitIndent(&out, indent);
                appendChar(&out, ';');
                ensureNewline(&out);
                atLineStart = true;
                pendingSpace = false;
                break;
            }
            case ',': {
                trimTrailingSpaces(&out);
                if (atLineStart) emitIndent(&out, indent);
                appendChar(&out, ',');
                appendChar(&out, ' ');
                atLineStart = false;
                pendingSpace = false;
                break;
            }
            case '(':
            case '[': {
                emitWordBoundarySpace(&out, &atLineStart, &pendingSpace);
                if (atLineStart) {
                    emitIndent(&out, indent);
                    atLineStart = false;
                }
                appendChar(&out, c);
                break;
            }
            case ')':
            case ']': {
                trimTrailingSpaces(&out);
                if (atLineStart) {
                    emitIndent(&out, indent);
                    atLineStart = false;
                }
                appendChar(&out, c);
                pendingSpace = false;
                break;
            }
            case '.': {
                trimTrailingSpaces(&out);
                if (atLineStart) {
                    emitIndent(&out, indent);
                    atLineStart = false;
                }
                appendChar(&out, '.');
                pendingSpace = false;
                break;
            }
            case '"': {
                emitWordBoundarySpace(&out, &atLineStart, &pendingSpace);
                if (atLineStart) {
                    emitIndent(&out, indent);
                    atLineStart = false;
                }
                appendChar(&out, '"');
                inString = true;
                escaped = false;
                break;
            }
            case '+':
            case '-':
            case '*':
            case '/':
            case '%':
            case '=':
            case '!':
            case '<':
            case '>': {
                bool hasEq = (i + 1 < len && src[i + 1] == '=');
                trimTrailingSpaces(&out);
                if (atLineStart) {
                    emitIndent(&out, indent);
                    atLineStart = false;
                }
                appendChar(&out, ' ');
                appendChar(&out, c);
                if (hasEq) {
                    appendChar(&out, '=');
                    i++;
                }
                appendChar(&out, ' ');
                pendingSpace = false;
                break;
            }
            default: {
                emitWordBoundarySpace(&out, &atLineStart, &pendingSpace);
                if (atLineStart) {
                    emitIndent(&out, indent);
                    atLineStart = false;
                }
                appendChar(&out, c);
                break;
            }
        }
    }

    ensureNewline(&out);
    *outLen = out.len;
    return out.data;
}

int main(int argc, char** argv) {
    bool writeInPlace = false;
    const char* path = NULL;

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: luxfmt [--write] <file.lux>\n");
        return 1;
    }

    if (argc == 3) {
        if (strcmp(argv[1], "--write") != 0) {
            fprintf(stderr, "Usage: luxfmt [--write] <file.lux>\n");
            return 1;
        }
        writeInPlace = true;
        path = argv[2];
    } else {
        path = argv[1];
    }

    size_t srcLen = 0;
    char* src = readFile(path, &srcLen);

    size_t outLen = 0;
    char* formatted = formatLux(src, srcLen, &outLen);
    free(src);

    if (writeInPlace) {
        writeFile(path, formatted, outLen);
    } else {
        fwrite(formatted, 1, outLen, stdout);
    }

    free(formatted);
    return 0;
}
