#include <u.h>
#include <libc.h>

typedef struct Buffer Buffer;
struct Buffer {
	char *data;
	long len;
	long cap;
};

static void
fail(char *msg)
{
	fprint(2, "luxfmt: %s\n", msg);
	exits("error");
}

static void
initBuffer(Buffer *b)
{
	b->cap = 4096;
	b->len = 0;
	b->data = malloc(b->cap);
	if (b->data == nil)
		fail("out of memory");
	b->data[0] = '\0';
}

static void
ensureCap(Buffer *b, long need)
{
	char *grown;

	if (need <= b->cap)
		return;
	while (b->cap < need)
		b->cap *= 2;
	grown = realloc(b->data, b->cap);
	if (grown == nil)
		fail("out of memory");
	b->data = grown;
}

static void
appendChar(Buffer *b, char c)
{
	ensureCap(b, b->len + 2);
	b->data[b->len++] = c;
	b->data[b->len] = '\0';
}

static void
appendStr(Buffer *b, char *s)
{
	long add;

	add = strlen(s);
	ensureCap(b, b->len + add + 1);
	memmove(b->data + b->len, s, add);
	b->len += add;
	b->data[b->len] = '\0';
}

static char
lastChar(Buffer *b)
{
	if (b->len == 0)
		return '\0';
	return b->data[b->len - 1];
}

static void
trimTrailingSpaces(Buffer *b)
{
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

static void
ensureNewline(Buffer *b)
{
	trimTrailingSpaces(b);
	if (lastChar(b) != '\n')
		appendChar(b, '\n');
}

static void
emitIndent(Buffer *out, int indent)
{
	int i;

	for (i = 0; i < indent; i++)
		appendStr(out, "    ");
}

static int
isSpaceChar(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static char*
skipSpaces(char *src, long len, long i)
{
	while (i < len && isSpaceChar(src[i]))
		i++;
	return src + i;
}

static int
nextIsChar(char *src, long len, long i, char needle)
{
	char *p = skipSpaces(src, len, i);
	return *p == needle;
}

static void
emitWordBoundarySpace(Buffer *out, int *atLineStart, int *pendingSpace)
{
	char last;

	if (!*pendingSpace || *atLineStart)
		goto done;
	last = lastChar(out);
	if (last != ' ' && last != '\n' && last != '(' && last != '[' && last != '{')
		appendChar(out, ' ');
done:
	*pendingSpace = 0;
}

static char*
readFile(char *path, long *outLen)
{
	int fd;
	Buffer in;
	long n;
	char tmp[4096];

	fd = open(path, OREAD);
	if (fd < 0)
		fail("unable to open input file");

	initBuffer(&in);
	for (;;) {
		n = read(fd, tmp, sizeof(tmp));
		if (n < 0) {
			close(fd);
			free(in.data);
			fail("unable to read input file");
		}
		if (n == 0)
			break;
		ensureCap(&in, in.len + n + 1);
		memmove(in.data + in.len, tmp, n);
		in.len += n;
		in.data[in.len] = '\0';
	}
	close(fd);
	*outLen = in.len;
	return in.data;
}

static void
writeFile(char *path, char *data, long len)
{
	int fd;
	long off, n;

	fd = create(path, OWRITE, 0666);
	if (fd < 0)
		fail("unable to open output file");

	off = 0;
	while (off < len) {
		n = write(fd, data + off, len - off);
		if (n <= 0) {
			close(fd);
			fail("unable to write output file");
		}
		off += n;
	}
	close(fd);
}

static char*
formatLux(char *src, long len, long *outLen)
{
	Buffer out;
	int inString, inLineComment, inBlockComment, escaped;
	int atLineStart, pendingSpace;
	int indent;
	long i;

	initBuffer(&out);
	inString = 0;
	inLineComment = 0;
	inBlockComment = 0;
	escaped = 0;
	atLineStart = 1;
	pendingSpace = 0;
	indent = 0;

	for (i = 0; i < len; i++) {
		char c = src[i];

		if (inString) {
			if (atLineStart) {
				emitIndent(&out, indent);
				atLineStart = 0;
			}
			appendChar(&out, c);
			if (escaped)
				escaped = 0;
			else if (c == '\\')
				escaped = 1;
			else if (c == '"')
				inString = 0;
			continue;
		}

		if (inLineComment) {
			if (atLineStart) {
				emitIndent(&out, indent);
				atLineStart = 0;
			}
			appendChar(&out, c);
			if (c == '\n') {
				inLineComment = 0;
				atLineStart = 1;
				pendingSpace = 0;
			}
			continue;
		}

		if (inBlockComment) {
			if (atLineStart) {
				emitIndent(&out, indent);
				atLineStart = 0;
			}
			appendChar(&out, c);
			if (c == '*' && i + 1 < len && src[i + 1] == '/') {
				appendChar(&out, '/');
				i++;
				inBlockComment = 0;
			}
			if (c == '\n')
				atLineStart = 1;
			continue;
		}

		if (isSpaceChar(c)) {
			if (c == '\n') {
				ensureNewline(&out);
				atLineStart = 1;
				pendingSpace = 0;
			} else {
				pendingSpace = 1;
			}
			continue;
		}

		if (c == '/' && i + 1 < len && src[i + 1] == '/') {
			emitWordBoundarySpace(&out, &atLineStart, &pendingSpace);
			if (atLineStart) {
				emitIndent(&out, indent);
				atLineStart = 0;
			}
			appendStr(&out, "//");
			i++;
			inLineComment = 1;
			continue;
		}

		if (c == '/' && i + 1 < len && src[i + 1] == '*') {
			emitWordBoundarySpace(&out, &atLineStart, &pendingSpace);
			if (atLineStart) {
				emitIndent(&out, indent);
				atLineStart = 0;
			}
			appendStr(&out, "/*");
			i++;
			inBlockComment = 1;
			continue;
		}

		switch (c) {
		case '{':
			trimTrailingSpaces(&out);
			if (!atLineStart && lastChar(&out) != ' ' && lastChar(&out) != '\n' && lastChar(&out) != '(')
				appendChar(&out, ' ');
			if (atLineStart)
				emitIndent(&out, indent);
			appendChar(&out, '{');
			ensureNewline(&out);
			indent++;
			atLineStart = 1;
			pendingSpace = 0;
			break;
		case '}':
			if (indent > 0)
				indent--;
			ensureNewline(&out);
			emitIndent(&out, indent);
			appendChar(&out, '}');
			if (!nextIsChar(src, len, i + 1, ';')) {
				ensureNewline(&out);
				atLineStart = 1;
			} else {
				atLineStart = 0;
			}
			pendingSpace = 0;
			break;
		case ';':
			trimTrailingSpaces(&out);
			if (atLineStart)
				emitIndent(&out, indent);
			appendChar(&out, ';');
			ensureNewline(&out);
			atLineStart = 1;
			pendingSpace = 0;
			break;
		case ',':
			trimTrailingSpaces(&out);
			if (atLineStart)
				emitIndent(&out, indent);
			appendChar(&out, ',');
			appendChar(&out, ' ');
			atLineStart = 0;
			pendingSpace = 0;
			break;
		case '(':
		case '[':
			emitWordBoundarySpace(&out, &atLineStart, &pendingSpace);
			if (atLineStart) {
				emitIndent(&out, indent);
				atLineStart = 0;
			}
			appendChar(&out, c);
			break;
		case ')':
		case ']':
			trimTrailingSpaces(&out);
			if (atLineStart) {
				emitIndent(&out, indent);
				atLineStart = 0;
			}
			appendChar(&out, c);
			pendingSpace = 0;
			break;
		case '.':
			trimTrailingSpaces(&out);
			if (atLineStart) {
				emitIndent(&out, indent);
				atLineStart = 0;
			}
			appendChar(&out, '.');
			pendingSpace = 0;
			break;
		case '"':
			emitWordBoundarySpace(&out, &atLineStart, &pendingSpace);
			if (atLineStart) {
				emitIndent(&out, indent);
				atLineStart = 0;
			}
			appendChar(&out, '"');
			inString = 1;
			escaped = 0;
			break;
		case '+':
		case '-':
		case '*':
		case '/':
		case '%':
		case '=':
		case '!':
		case '<':
		case '>': {
			int hasEq;

			hasEq = (i + 1 < len && src[i + 1] == '=');
			trimTrailingSpaces(&out);
			if (atLineStart) {
				emitIndent(&out, indent);
				atLineStart = 0;
			}
			appendChar(&out, ' ');
			appendChar(&out, c);
			if (hasEq) {
				appendChar(&out, '=');
				i++;
			}
			appendChar(&out, ' ');
			pendingSpace = 0;
			break;
		}
		default:
			emitWordBoundarySpace(&out, &atLineStart, &pendingSpace);
			if (atLineStart) {
				emitIndent(&out, indent);
				atLineStart = 0;
			}
			appendChar(&out, c);
			break;
		}
	}

	ensureNewline(&out);
	*outLen = out.len;
	return out.data;
}

void
usage(void)
{
	fprint(2, "usage: luxfmt_plan9 [-w] file.lux\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	int writeInPlace;
	char *path;
	char *src, *formatted;
	long srcLen, outLen;

	writeInPlace = 0;
	if (argc < 2 || argc > 3)
		usage();

	if (argc == 3) {
		if (strcmp(argv[1], "-w") != 0)
			usage();
		writeInPlace = 1;
		path = argv[2];
	} else {
		path = argv[1];
	}

	src = readFile(path, &srcLen);
	formatted = formatLux(src, srcLen, &outLen);
	free(src);

	if (writeInPlace)
		writeFile(path, formatted, outLen);
	else
		write(1, formatted, outLen);

	free(formatted);
	exits(nil);
}
