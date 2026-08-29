#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "value.h"
#include "object.h"
#include "vm.h"
#include "table.h"
#include "file_native.h"

#define FILE_CHUNK 8192
#define FILE_LINE_MAX (32 * 1024 * 1024)

typedef struct {
	FILE* fp;
	char* buf;
	int len;
	int cap;
	int pos;
	int eof;
	int bad;
	int closed;
} FileReader;

static ObjClass* fileClass = NULL;

void fileSetClass(ObjClass* klass) {
	fileClass = klass;
}

static void setOk(ObjInstance* inst, bool ok) {
	tableSet(&inst->fields, copyString("ok", 2), BOOL_VAL(ok));
}

static void setPtr(ObjInstance* inst, FileReader* r) {
	if (r == NULL) {
		tableSet(&inst->fields, copyString("_ptr", 4), NIL_VAL);
		return;
	}
	tableSet(&inst->fields, copyString("_ptr", 4),
		NUMBER_VAL((double)(uintptr_t)r));
}

static FileReader* getReader(ObjInstance* inst) {
	Value v;
	if (inst == NULL) return NULL;
	if (fileClass == NULL || inst->klass != fileClass) return NULL;
	if (!tableGet(&inst->fields, copyString("_ptr", 4), &v)) return NULL;
	if (!IS_NUMBER(v)) return NULL;
	return (FileReader*)(uintptr_t)AS_NUMBER(v);
}

static void freeReader(FileReader* r) {
	if (r == NULL) return;
	if (r->fp != NULL) {
		fclose(r->fp);
		r->fp = NULL;
	}
	r->closed = 1;
	free(r->buf);
	free(r);
}

static FileReader* readerFromFields(ObjInstance* inst) {
	int i;
	if (inst == NULL || inst->fields.entries == NULL) return NULL;
	for (i = 0; i < inst->fields.capacity; i++) {
		Entry* e = &inst->fields.entries[i];
		if (e->key != NULL && e->key->length == 4 &&
		    memcmp(e->key->chars, "_ptr", 4) == 0) {
			if (!IS_NUMBER(e->value)) return NULL;
			return (FileReader*)(uintptr_t)AS_NUMBER(e->value);
		}
	}
	return NULL;
}

void fileCloseFromInstance(ObjInstance* instance) {
	FileReader* r;
	if (instance == NULL) return;
	if (fileClass == NULL || instance->klass != fileClass) return;
	r = readerFromFields(instance);
	if (r == NULL) return;
	freeReader(r);
}

static void compact(FileReader* r) {
	if (r->pos <= 0) return;
	if (r->len > r->pos)
		memmove(r->buf, r->buf + r->pos, (size_t)(r->len - r->pos));
	r->len -= r->pos;
	r->pos = 0;
}

static int growBuf(FileReader* r) {
	int ncap;
	char* nbuf;
	if (r->cap >= FILE_LINE_MAX) return 0;
	ncap = r->cap * 2;
	if (ncap > FILE_LINE_MAX) ncap = FILE_LINE_MAX;
	if (ncap <= r->cap) return 0;
	nbuf = (char*)realloc(r->buf, (size_t)ncap);
	if (nbuf == NULL) return 0;
	r->buf = nbuf;
	r->cap = ncap;
	return 1;
}

static int fill(FileReader* r) {
	size_t n;
	compact(r);
	if (r->len == r->cap) {
		if (!growBuf(r)) {
			r->bad = 1;
			return -1;
		}
	}
	n = fread(r->buf + r->len, 1, (size_t)(r->cap - r->len), r->fp);
	if (n == 0) {
		r->eof = 1;
		return 0;
	}
	r->len += (int)n;
	return (int)n;
}

static int findNl(FileReader* r) {
	int i;
	for (i = r->pos; i < r->len; i++) {
		if (r->buf[i] == '\n') return i;
	}
	return -1;
}

static Value takeRange(FileReader* r, int start, int end, int nextPos) {
	if (end > start && r->buf[end - 1] == '\r')
		end--;
	r->pos = nextPos;
	return OBJ_VAL(copyString(r->buf + start, end - start));
}

Value fileInitNative(int argCount, Value* args) {
	ObjInstance* inst;
	FileReader* r;
	FILE* fp;
	char* path;

	if (argCount < 1 || !IS_INSTANCE(args[0]))
		return NIL_VAL;
	inst = AS_INSTANCE(args[0]);
	setOk(inst, false);
	setPtr(inst, NULL);

	if (argCount < 2 || !IS_STRING(args[1]))
		return args[0];
	path = AS_CSTRING(args[1]);
	fp = fopen(path, "rb");
	if (fp == NULL)
		return args[0];

	r = (FileReader*)malloc(sizeof(FileReader));
	if (r == NULL) {
		fclose(fp);
		return args[0];
	}
	r->buf = (char*)malloc(FILE_CHUNK);
	if (r->buf == NULL) {
		fclose(fp);
		free(r);
		return args[0];
	}
	r->fp = fp;
	r->len = 0;
	r->cap = FILE_CHUNK;
	r->pos = 0;
	r->eof = 0;
	r->bad = 0;
	r->closed = 0;
	setPtr(inst, r);
	setOk(inst, true);
	return args[0];
}

Value fileReadLineNative(int argCount, Value* args) {
	ObjInstance* inst;
	FileReader* r;
	int nl;

	if (argCount != 1 || !IS_INSTANCE(args[0]))
		return NIL_VAL;
	inst = AS_INSTANCE(args[0]);
	r = getReader(inst);
	if (r == NULL || r->closed || r->bad)
		return NIL_VAL;

	for (;;) {
		nl = findNl(r);
		if (nl >= 0)
			return takeRange(r, r->pos, nl, nl + 1);
		if (r->eof) {
			if (r->pos < r->len)
				return takeRange(r, r->pos, r->len, r->len);
			return NIL_VAL;
		}
		if (fill(r) < 0)
			return NIL_VAL;
	}
}

Value fileCloseNative(int argCount, Value* args) {
	ObjInstance* inst;
	FileReader* r;

	if (argCount != 1 || !IS_INSTANCE(args[0]))
		return BOOL_VAL(false);
	inst = AS_INSTANCE(args[0]);
	r = getReader(inst);
	if (r == NULL)
		return BOOL_VAL(false);
	setPtr(inst, NULL);
	setOk(inst, false);
	freeReader(r);
	return BOOL_VAL(true);
}
