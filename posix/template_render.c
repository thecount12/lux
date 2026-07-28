#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "value.h"
#include "object.h"
#include "table.h"
#include "template_render.h"

/*
 * Native template renderer.
 *   {{ key }}                 HTML-escaped field on ctx
 *   {% for x in items %}...   non-nested array loop
 *   {% include "file.tpl" %}  relative include; cached until process exit
 */

typedef struct TplBuf {
	char* data;
	int len;
	int cap;
} TplBuf;

typedef struct TplCacheEntry {
	char* path;
	char* content;
	struct TplCacheEntry* next;
} TplCacheEntry;

static TplCacheEntry* tplCacheHead = NULL;

static int
tplBufInit(TplBuf* b)
{
	b->cap = 256;
	b->len = 0;
	b->data = malloc(b->cap);
	if (b->data == NULL) return 0;
	b->data[0] = '\0';
	return 1;
}

static void
tplBufFree(TplBuf* b)
{
	if (b->data != NULL) free(b->data);
	b->data = NULL;
	b->len = 0;
	b->cap = 0;
}

static int
tplBufGrow(TplBuf* b, int need)
{
	char* nd;
	int ncap;

	if (b->len + need + 1 <= b->cap) return 1;
	ncap = b->cap * 2;
	if (ncap < b->len + need + 1) ncap = b->len + need + 1;
	nd = realloc(b->data, ncap);
	if (nd == NULL) return 0;
	b->data = nd;
	b->cap = ncap;
	return 1;
}

static int
tplBufAppend(TplBuf* b, char* s, int n)
{
	if (s == NULL) return 1;
	if (n < 0) n = (int)strlen(s);
	if (!tplBufGrow(b, n)) return 0;
	memcpy(b->data + b->len, s, (unsigned long)n);
	b->len += n;
	b->data[b->len] = '\0';
	return 1;
}

static int
tplBufAppendEscaped(TplBuf* b, char* s, int n)
{
	int i;
	char c;

	if (s == NULL) return 1;
	if (n < 0) n = (int)strlen(s);
	for (i = 0; i < n; i++) {
		c = s[i];
		if (c == '&') {
			if (!tplBufAppend(b, "&amp;", 5)) return 0;
		} else if (c == '<') {
			if (!tplBufAppend(b, "&lt;", 4)) return 0;
		} else if (c == '>') {
			if (!tplBufAppend(b, "&gt;", 4)) return 0;
		} else if (c == '"') {
			if (!tplBufAppend(b, "&quot;", 6)) return 0;
		} else if (c == '\'') {
			if (!tplBufAppend(b, "&#39;", 5)) return 0;
		} else {
			if (!tplBufAppend(b, &c, 1)) return 0;
		}
	}
	return 1;
}

static char*
tplDupStr(char* s, int n)
{
	char* out;

	if (s == NULL) return NULL;
	if (n < 0) n = (int)strlen(s);
	out = malloc((unsigned long)n + 1);
	if (out == NULL) return NULL;
	memcpy(out, s, (unsigned long)n);
	out[n] = '\0';
	return out;
}

static char*
tplCacheGet(char* path)
{
	TplCacheEntry* e;

	for (e = tplCacheHead; e != NULL; e = e->next) {
		if (strcmp(e->path, path) == 0) return e->content;
	}
	return NULL;
}

static void
tplCachePut(char* path, char* content)
{
	TplCacheEntry* e;
	char* pcopy;
	char* ccopy;

	if (tplCacheGet(path) != NULL) return;
	pcopy = tplDupStr(path, -1);
	ccopy = tplDupStr(content, -1);
	if (pcopy == NULL || ccopy == NULL) {
		free(pcopy);
		free(ccopy);
		return;
	}
	e = malloc(sizeof(TplCacheEntry));
	if (e == NULL) {
		free(pcopy);
		free(ccopy);
		return;
	}
	e->path = pcopy;
	e->content = ccopy;
	e->next = tplCacheHead;
	tplCacheHead = e;
}

static char*
tplLoadFile(char* path, int* outLen)
{
	char* buf;
	int len;
	FILE* file;
	long fileSize;
	long bytesRead;

	file = fopen(path, "rb");
	if (file == NULL) return NULL;
	fseek(file, 0L, SEEK_END);
	fileSize = ftell(file);
	rewind(file);
	if (fileSize < 0) {
		fclose(file);
		return NULL;
	}
	buf = malloc((unsigned long)fileSize + 1);
	if (buf == NULL) {
		fclose(file);
		return NULL;
	}
	bytesRead = (long)fread(buf, 1, (unsigned long)fileSize, file);
	buf[bytesRead] = '\0';
	fclose(file);
	len = (int)bytesRead;
	if (outLen != NULL) *outLen = len;
	return buf;
}

static int
tplLastSlash(char* path)
{
	int i;
	int last;

	last = -1;
	for (i = 0; path[i] != '\0'; i++) {
		if (path[i] == '/') last = i;
	}
	return last;
}

static void
tplTrimInPlace(char* s)
{
	char* start;
	char* end;
	int n;

	if (s == NULL) return;
	start = s;
	while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r')
		start++;
	if (start != s) {
		n = (int)strlen(start);
		memmove(s, start, (unsigned long)n + 1);
	}
	n = (int)strlen(s);
	end = s + n;
	while (end > s && (end[-1] == ' ' || end[-1] == '\t' ||
	                   end[-1] == '\n' || end[-1] == '\r'))
		end--;
	*end = '\0';
}

static char* tplExpandIncludesDepth(char* path, int depth);

static char*
tplExpandIncludesUncached(char* path, int depth)
{
	char* src;
	int srcLen;
	TplBuf out;
	int cursor;
	char* p;
	char msg[512];
	int rem;
	int incStart;
	int quoteStart;
	int quoteEnd;
	int tagEnd;
	int slashIdx;
	char* incPath;
	char fullInc[2048];
	char* incContent;
	int baseLen;

	src = tplLoadFile(path, &srcLen);
	if (src == NULL) {
		snprintf(msg, sizeof(msg), "Template not found: %s", path);
		return tplDupStr(msg, -1);
	}

	if (!tplBufInit(&out)) {
		free(src);
		return NULL;
	}

	cursor = 0;
	while (1) {
		rem = srcLen - cursor;
		if (rem <= 0) break;
		p = strstr(src + cursor, "{% include \"");
		if (p == NULL) {
			if (!tplBufAppend(&out, src + cursor, rem)) {
				tplBufFree(&out);
				free(src);
				return NULL;
			}
			break;
		}
		incStart = (int)(p - src);
		if (!tplBufAppend(&out, src + cursor, incStart - cursor)) {
			tplBufFree(&out);
			free(src);
			return NULL;
		}
		quoteStart = incStart + (int)strlen("{% include \"");
		p = strchr(src + quoteStart, '"');
		if (p == NULL) {
			if (!tplBufAppend(&out, src + incStart, srcLen - incStart)) {
				tplBufFree(&out);
				free(src);
				return NULL;
			}
			break;
		}
		quoteEnd = (int)(p - src);
		incPath = tplDupStr(src + quoteStart, quoteEnd - quoteStart);
		if (incPath == NULL) {
			tplBufFree(&out);
			free(src);
			return NULL;
		}
		slashIdx = tplLastSlash(path);
		baseLen = 0;
		fullInc[0] = '\0';
		if (slashIdx >= 0) {
			baseLen = slashIdx + 1;
			if (baseLen >= (int)sizeof(fullInc)) baseLen = (int)sizeof(fullInc) - 1;
			memcpy(fullInc, path, (unsigned long)baseLen);
			fullInc[baseLen] = '\0';
		}
		if (baseLen + (int)strlen(incPath) >= (int)sizeof(fullInc)) {
			free(incPath);
			tplBufFree(&out);
			free(src);
			return NULL;
		}
		strcpy(fullInc + baseLen, incPath);
		free(incPath);

		p = strstr(src + quoteEnd, "%}");
		if (p == NULL) {
			if (!tplBufAppend(&out, src + incStart, srcLen - incStart)) {
				tplBufFree(&out);
				free(src);
				return NULL;
			}
			break;
		}
		tagEnd = (int)(p - src) + 2;

		incContent = tplExpandIncludesDepth(fullInc, depth + 1);
		if (incContent == NULL) {
			tplBufFree(&out);
			free(src);
			return NULL;
		}
		if (!tplBufAppend(&out, incContent, -1)) {
			free(incContent);
			tplBufFree(&out);
			free(src);
			return NULL;
		}
		free(incContent);
		cursor = tagEnd;
	}

	free(src);
	src = out.data;
	out.data = NULL;
	return src;
}

static char*
tplExpandIncludesDepth(char* path, int depth)
{
	char* cached;
	char* expanded;

	if (depth > 32)
		return tplDupStr("Template include depth exceeded", -1);
	cached = tplCacheGet(path);
	if (cached != NULL) return tplDupStr(cached, -1);
	expanded = tplExpandIncludesUncached(path, depth);
	if (expanded != NULL) tplCachePut(path, expanded);
	return expanded;
}

static int
tplLookupField(ObjInstance* ctx, char* key, Value* out)
{
	ObjString* name;

	if (ctx == NULL || key == NULL || key[0] == '\0') return 0;
	name = copyString(key, (int)strlen(key));
	if (!tableGet(&ctx->fields, name, out)) return 0;
	return 1;
}

static int
tplAppendValuePlain(TplBuf* b, Value val)
{
	char numBuf[64];
	int n;
	ObjString* s;

	if (IS_NIL(val)) return 1;
	if (IS_BOOL(val))
		return tplBufAppend(b, AS_BOOL(val) ? "true" : "false", -1);
	if (IS_NUMBER(val)) {
		n = snprintf(numBuf, sizeof(numBuf), "%.15g", AS_NUMBER(val));
		if (n < 0) n = 0;
		return tplBufAppend(b, numBuf, n);
	}
	if (IS_STRING(val)) {
		s = AS_STRING(val);
		return tplBufAppend(b, s->chars, s->length);
	}
	return tplBufAppend(b, "[object]", 8);
}

static int
tplAppendValueEscaped(TplBuf* b, Value val)
{
	TplBuf tmp;

	if (IS_NIL(val)) return 1;
	if (!tplBufInit(&tmp)) return 0;
	if (!tplAppendValuePlain(&tmp, val)) {
		tplBufFree(&tmp);
		return 0;
	}
	if (!tplBufAppendEscaped(b, tmp.data, tmp.len)) {
		tplBufFree(&tmp);
		return 0;
	}
	tplBufFree(&tmp);
	return 1;
}

static char*
tplInterpolate(char* src, ObjInstance* ctx, char* loopVar, Value loopVal)
{
	TplBuf out;
	int cursor;
	int srcLen;
	char* p;
	int openIdx;
	int closeIdx;
	char keyBuf[256];
	int keyLen;
	Value val;

	if (src == NULL) return tplDupStr("", 0);
	srcLen = (int)strlen(src);
	if (!tplBufInit(&out)) return NULL;

	cursor = 0;
	while (cursor < srcLen) {
		p = strstr(src + cursor, "{{");
		if (p == NULL) {
			if (!tplBufAppend(&out, src + cursor, srcLen - cursor)) {
				tplBufFree(&out);
				return NULL;
			}
			break;
		}
		openIdx = (int)(p - src);
		if (!tplBufAppend(&out, src + cursor, openIdx - cursor)) {
			tplBufFree(&out);
			return NULL;
		}
		p = strstr(src + openIdx + 2, "}}");
		if (p == NULL) {
			if (!tplBufAppend(&out, src + openIdx, srcLen - openIdx)) {
				tplBufFree(&out);
				return NULL;
			}
			break;
		}
		closeIdx = (int)(p - src);
		keyLen = closeIdx - (openIdx + 2);
		if (keyLen < 0) keyLen = 0;
		if (keyLen >= (int)sizeof(keyBuf)) keyLen = (int)sizeof(keyBuf) - 1;
		memcpy(keyBuf, src + openIdx + 2, (unsigned long)keyLen);
		keyBuf[keyLen] = '\0';
		tplTrimInPlace(keyBuf);

		if (loopVar != NULL && strcmp(keyBuf, loopVar) == 0) {
			if (!tplAppendValueEscaped(&out, loopVal)) {
				tplBufFree(&out);
				return NULL;
			}
		} else if (loopVar != NULL && ctx == NULL) {
			if (!tplBufAppend(&out, src + openIdx, closeIdx + 2 - openIdx)) {
				tplBufFree(&out);
				return NULL;
			}
		} else if (ctx != NULL) {
			if (tplLookupField(ctx, keyBuf, &val)) {
				if (!tplAppendValueEscaped(&out, val)) {
					tplBufFree(&out);
					return NULL;
				}
			}
		}
		cursor = closeIdx + 2;
	}

	p = out.data;
	out.data = NULL;
	return p;
}

static char*
tplRenderLoopPiece(char* inner, char* varName, Value elem, ObjInstance* ctx)
{
	char* replaced;
	char* piece;

	replaced = tplInterpolate(inner, NULL, varName, elem);
	if (replaced == NULL) return NULL;
	piece = tplInterpolate(replaced, ctx, NULL, NIL_VAL);
	free(replaced);
	return piece;
}

static char*
tplRenderFors(char* srcIn, ObjInstance* ctx)
{
	char* src;
	int cursor;
	TplBuf rebuilt;
	char* p;
	int forStart;
	int forTagEnd;
	int endTag;
	int inIdx;
	char* forExpr;
	char varName[128];
	char iterableKey[128];
	char* inner;
	char* rendered;
	char* nextSrc;
	TplBuf pieceBuf;
	Value itemsVal;
	ObjArray* arr;
	int i;
	int srcLen;
	int exprLen;
	int afterFor;
	int endforLen;
	char* rest;
	int rlen;
	char* piece;
	int suffixLen;
	int renderedLen;
	char* rendPtr;

	src = tplDupStr(srcIn, -1);
	if (src == NULL) return NULL;

	cursor = 0;
	while (1) {
		srcLen = (int)strlen(src);
		p = strstr(src + cursor, "{% for ");
		if (p == NULL) break;
		forStart = (int)(p - src);
		p = strstr(src + forStart, "%}");
		if (p == NULL) break;
		forTagEnd = (int)(p - src);

		exprLen = forTagEnd - (forStart + (int)strlen("{% for "));
		if (exprLen < 0) exprLen = 0;
		forExpr = tplDupStr(src + forStart + (int)strlen("{% for "), exprLen);
		if (forExpr == NULL) {
			free(src);
			return NULL;
		}
		p = strstr(forExpr, " in ");
		if (p == NULL) {
			free(forExpr);
			cursor = forTagEnd + 2;
			continue;
		}
		inIdx = (int)(p - forExpr);
		if (inIdx >= (int)sizeof(varName)) inIdx = (int)sizeof(varName) - 1;
		memcpy(varName, forExpr, (unsigned long)inIdx);
		varName[inIdx] = '\0';
		tplTrimInPlace(varName);
		rest = forExpr + inIdx + (int)strlen(" in ");
		rlen = (int)strlen(rest);
		if (rlen >= (int)sizeof(iterableKey)) rlen = (int)sizeof(iterableKey) - 1;
		memcpy(iterableKey, rest, (unsigned long)rlen);
		iterableKey[rlen] = '\0';
		tplTrimInPlace(iterableKey);
		free(forExpr);

		p = strstr(src + forTagEnd, "{% endfor %}");
		if (p == NULL) break;
		endTag = (int)(p - src);
		afterFor = forTagEnd + 2;
		inner = tplDupStr(src + afterFor, endTag - afterFor);
		if (inner == NULL) {
			free(src);
			return NULL;
		}

		if (!tplBufInit(&pieceBuf)) {
			free(inner);
			free(src);
			return NULL;
		}
		if (ctx != NULL && tplLookupField(ctx, iterableKey, &itemsVal) &&
		    IS_ARRAY(itemsVal)) {
			arr = AS_ARRAY(itemsVal);
			for (i = 0; i < arr->count; i++) {
				piece = tplRenderLoopPiece(inner, varName, arr->elements[i], ctx);
				if (piece == NULL) {
					tplBufFree(&pieceBuf);
					free(inner);
					free(src);
					return NULL;
				}
				if (!tplBufAppend(&pieceBuf, piece, -1)) {
					free(piece);
					tplBufFree(&pieceBuf);
					free(inner);
					free(src);
					return NULL;
				}
				free(piece);
			}
		}
		free(inner);
		rendered = pieceBuf.data;
		pieceBuf.data = NULL;

		endforLen = (int)strlen("{% endfor %}");
		if (!tplBufInit(&rebuilt)) {
			free(rendered);
			free(src);
			return NULL;
		}
		rendPtr = rendered;
		if (rendPtr == NULL) rendPtr = "";
		if (!tplBufAppend(&rebuilt, src, forStart) ||
		    !tplBufAppend(&rebuilt, rendPtr, -1) ||
		    !tplBufAppend(&rebuilt, src + endTag + endforLen, -1)) {
			free(rendered);
			tplBufFree(&rebuilt);
			free(src);
			return NULL;
		}
		suffixLen = srcLen - (endTag + endforLen);
		renderedLen = rebuilt.len - forStart - suffixLen;
		cursor = forStart + renderedLen;
		free(rendered);
		nextSrc = rebuilt.data;
		rebuilt.data = NULL;
		free(src);
		src = nextSrc;
	}

	return src;
}

char*
tplRenderFull(char* path, ObjInstance* ctx)
{
	char* expanded;
	char* withFors;
	char* final;

	expanded = tplExpandIncludesDepth(path, 0);
	if (expanded == NULL) return NULL;
	withFors = tplRenderFors(expanded, ctx);
	free(expanded);
	if (withFors == NULL) return NULL;
	final = tplInterpolate(withFors, ctx, NULL, NIL_VAL);
	free(withFors);
	return final;
}
