/*
 * Native template renderer — included from vm.c (Plan 9) and posix/vm.c.
 * Define LUX_TPL_POSIX before including on POSIX.
 *
 * Syntax:
 *   {{ key }}                  HTML-escaped field on ctx instance
 *   {% for x in items %}...{% endfor %}   non-nested array loop
 *   {% include "file.tpl" %}   relative to including file; cached
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

static TplCacheEntry* tplCacheHead =
#ifdef LUX_TPL_POSIX
	NULL;
#else
	nil;
#endif

#ifdef LUX_TPL_POSIX
#define TPL_NULL NULL
#define tplSnprint snprintf
#else
#define TPL_NULL nil
#define tplSnprint snprint
#endif

static int
tplBufInit(TplBuf* b)
{
	b->cap = 256;
	b->len = 0;
	b->data = (char*)malloc(b->cap);
	if (b->data == TPL_NULL) return 0;
	b->data[0] = '\0';
	return 1;
}

static void
tplBufFree(TplBuf* b)
{
	if (b->data != TPL_NULL) free(b->data);
	b->data = TPL_NULL;
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
	nd = (char*)realloc(b->data, ncap);
	if (nd == TPL_NULL) return 0;
	b->data = nd;
	b->cap = ncap;
	return 1;
}

static int
tplBufAppend(TplBuf* b, const char* s, int n)
{
	if (s == TPL_NULL) return 1;
	if (n < 0) n = (int)strlen(s);
	if (!tplBufGrow(b, n)) return 0;
	memcpy(b->data + b->len, s, (size_t)n);
	b->len += n;
	b->data[b->len] = '\0';
	return 1;
}

static int
tplBufAppendEscaped(TplBuf* b, const char* s, int n)
{
	int i;
	if (s == TPL_NULL) return 1;
	if (n < 0) n = (int)strlen(s);
	for (i = 0; i < n; i++) {
		char c = s[i];
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
tplDupStr(const char* s, int n)
{
	char* out;
	if (s == TPL_NULL) return TPL_NULL;
	if (n < 0) n = (int)strlen(s);
	out = (char*)malloc((size_t)n + 1);
	if (out == TPL_NULL) return TPL_NULL;
	memcpy(out, s, (size_t)n);
	out[n] = '\0';
	return out;
}

static char*
tplCacheGet(const char* path)
{
	TplCacheEntry* e;
	for (e = tplCacheHead; e != TPL_NULL; e = e->next) {
		if (strcmp(e->path, path) == 0) return e->content;
	}
	return TPL_NULL;
}

static void
tplCachePut(const char* path, const char* content)
{
	TplCacheEntry* e;
	char* pcopy;
	char* ccopy;
	if (tplCacheGet(path) != TPL_NULL) return;
	pcopy = tplDupStr(path, -1);
	ccopy = tplDupStr(content, -1);
	if (pcopy == TPL_NULL || ccopy == TPL_NULL) {
		free(pcopy);
		free(ccopy);
		return;
	}
	e = (TplCacheEntry*)malloc(sizeof(TplCacheEntry));
	if (e == TPL_NULL) {
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
tplLoadFile(const char* path, int* outLen)
{
	char* buf;
	int len;
#ifdef LUX_TPL_POSIX
	FILE* file;
	size_t fileSize;
	size_t bytesRead;

	file = fopen(path, "rb");
	if (file == NULL) return NULL;
	fseek(file, 0L, SEEK_END);
	fileSize = (size_t)ftell(file);
	rewind(file);
	buf = (char*)malloc(fileSize + 1);
	if (buf == NULL) {
		fclose(file);
		return NULL;
	}
	bytesRead = fread(buf, 1, fileSize, file);
	buf[bytesRead] = '\0';
	fclose(file);
	len = (int)bytesRead;
#else
	int fd;
	Dir* d;
	long bytesRead;

	fd = open(path, OREAD);
	if (fd < 0) return nil;
	d = dirfstat(fd);
	if (d == nil) {
		close(fd);
		return nil;
	}
	len = (int)d->length;
	free(d);
	buf = (char*)malloc((ulong)len + 1);
	if (buf == nil) {
		close(fd);
		return nil;
	}
	bytesRead = read(fd, buf, len);
	close(fd);
	if (bytesRead < 0) {
		free(buf);
		return nil;
	}
	buf[bytesRead] = '\0';
	len = (int)bytesRead;
#endif
	if (outLen != TPL_NULL) *outLen = len;
	return buf;
}

static int
tplLastSlash(const char* path)
{
	int i;
	int last = -1;
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
	if (s == TPL_NULL) return;
	start = s;
	while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r')
		start++;
	if (start != s) {
		n = (int)strlen(start);
		memmove(s, start, (size_t)n + 1);
	}
	n = (int)strlen(s);
	end = s + n;
	while (end > s && (end[-1] == ' ' || end[-1] == '\t' ||
	                   end[-1] == '\n' || end[-1] == '\r'))
		end--;
	*end = '\0';
}

static char* tplExpandIncludesDepth(const char* path, int depth);

static char*
tplExpandIncludesUncached(const char* path, int depth)
{
	char* src;
	int srcLen;
	TplBuf out;
	int cursor;
	char* p;
	char msg[512];

	src = tplLoadFile(path, &srcLen);
	if (src == TPL_NULL) {
		tplSnprint(msg, sizeof(msg), "Template not found: %s", path);
		return tplDupStr(msg, -1);
	}

	if (!tplBufInit(&out)) {
		free(src);
		return TPL_NULL;
	}

	cursor = 0;
	while (1) {
		int rem = srcLen - cursor;
		int incStart;
		int quoteStart;
		int quoteEnd;
		int tagEnd;
		int slashIdx;
		char* incPath;
		char fullInc[2048];
		char* incContent;
		int baseLen;

		if (rem <= 0) break;
		p = strstr(src + cursor, "{% include \"");
		if (p == TPL_NULL) {
			if (!tplBufAppend(&out, src + cursor, rem)) {
				tplBufFree(&out);
				free(src);
				return TPL_NULL;
			}
			break;
		}
		incStart = (int)(p - src);
		if (!tplBufAppend(&out, src + cursor, incStart - cursor)) {
			tplBufFree(&out);
			free(src);
			return TPL_NULL;
		}
		quoteStart = incStart + (int)strlen("{% include \"");
		p = strchr(src + quoteStart, '"');
		if (p == TPL_NULL) {
			if (!tplBufAppend(&out, src + incStart, srcLen - incStart)) {
				tplBufFree(&out);
				free(src);
				return TPL_NULL;
			}
			break;
		}
		quoteEnd = (int)(p - src);
		incPath = tplDupStr(src + quoteStart, quoteEnd - quoteStart);
		if (incPath == TPL_NULL) {
			tplBufFree(&out);
			free(src);
			return TPL_NULL;
		}
		slashIdx = tplLastSlash(path);
		baseLen = 0;
		fullInc[0] = '\0';
		if (slashIdx >= 0) {
			baseLen = slashIdx + 1;
			if (baseLen >= (int)sizeof(fullInc)) baseLen = (int)sizeof(fullInc) - 1;
			memcpy(fullInc, path, (size_t)baseLen);
			fullInc[baseLen] = '\0';
		}
		if (baseLen + (int)strlen(incPath) >= (int)sizeof(fullInc)) {
			free(incPath);
			tplBufFree(&out);
			free(src);
			return TPL_NULL;
		}
		strcpy(fullInc + baseLen, incPath);
		free(incPath);

		p = strstr(src + quoteEnd, "%}");
		if (p == TPL_NULL) {
			if (!tplBufAppend(&out, src + incStart, srcLen - incStart)) {
				tplBufFree(&out);
				free(src);
				return TPL_NULL;
			}
			break;
		}
		tagEnd = (int)(p - src) + 2;

		incContent = tplExpandIncludesDepth(fullInc, depth + 1);
		if (incContent == TPL_NULL) {
			tplBufFree(&out);
			free(src);
			return TPL_NULL;
		}
		if (!tplBufAppend(&out, incContent, -1)) {
			free(incContent);
			tplBufFree(&out);
			free(src);
			return TPL_NULL;
		}
		free(incContent);
		cursor = tagEnd;
	}

	free(src);
	src = out.data;
	out.data = TPL_NULL;
	return src;
}

static char*
tplExpandIncludesDepth(const char* path, int depth)
{
	char* cached;
	char* expanded;
	if (depth > 32) {
		return tplDupStr("Template include depth exceeded", -1);
	}
	cached = tplCacheGet(path);
	if (cached != TPL_NULL) return tplDupStr(cached, -1);
	expanded = tplExpandIncludesUncached(path, depth);
	if (expanded != TPL_NULL) tplCachePut(path, expanded);
	return expanded;
}

static int
tplLookupField(ObjInstance* ctx, const char* key, Value* out)
{
	ObjString* name;
	if (ctx == TPL_NULL || key == TPL_NULL || key[0] == '\0') return 0;
	name = copyString(key, (int)strlen(key));
	if (!tableGet(&ctx->fields, name, out)) return 0;
	return 1;
}

/* Write plain (unescaped) text representation into buf; returns 0 on OOM. */
static int
tplAppendValuePlain(TplBuf* b, Value val)
{
	char numBuf[64];
	int n;
	ObjString* s;
	if (IS_NIL(val)) return 1;
	if (IS_BOOL(val)) {
		return tplBufAppend(b, AS_BOOL(val) ? "true" : "false", -1);
	}
	if (IS_NUMBER(val)) {
		n = tplSnprint(numBuf, sizeof(numBuf), "%.15g", AS_NUMBER(val));
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

/*
 * Interpolate {{ key }}.
 * If loopVar != NULL and key matches, use loopVal (escaped).
 * Else look up ctx field (escaped). Missing -> empty.
 */
static char*
tplInterpolate(const char* src, ObjInstance* ctx, const char* loopVar, Value loopVal)
{
	TplBuf out;
	int cursor;
	int srcLen;
	char* p;

	if (src == TPL_NULL) return tplDupStr("", 0);
	srcLen = (int)strlen(src);
	if (!tplBufInit(&out)) return TPL_NULL;

	cursor = 0;
	while (cursor < srcLen) {
		int openIdx;
		int closeIdx;
		char keyBuf[256];
		int keyLen;

		p = strstr(src + cursor, "{{");
		if (p == TPL_NULL) {
			if (!tplBufAppend(&out, src + cursor, srcLen - cursor)) {
				tplBufFree(&out);
				return TPL_NULL;
			}
			break;
		}
		openIdx = (int)(p - src);
		if (!tplBufAppend(&out, src + cursor, openIdx - cursor)) {
			tplBufFree(&out);
			return TPL_NULL;
		}
		p = strstr(src + openIdx + 2, "}}");
		if (p == TPL_NULL) {
			if (!tplBufAppend(&out, src + openIdx, srcLen - openIdx)) {
				tplBufFree(&out);
				return TPL_NULL;
			}
			break;
		}
		closeIdx = (int)(p - src);
		keyLen = closeIdx - (openIdx + 2);
		if (keyLen < 0) keyLen = 0;
		if (keyLen >= (int)sizeof(keyBuf)) keyLen = (int)sizeof(keyBuf) - 1;
		memcpy(keyBuf, src + openIdx + 2, (size_t)keyLen);
		keyBuf[keyLen] = '\0';
		tplTrimInPlace(keyBuf);

		if (loopVar != TPL_NULL && strcmp(keyBuf, loopVar) == 0) {
			if (!tplAppendValueEscaped(&out, loopVal)) {
				tplBufFree(&out);
				return TPL_NULL;
			}
		} else if (loopVar != TPL_NULL && ctx == TPL_NULL) {
			/* replaceLoopVar pass: keep other {{ tags }} for later */
			if (!tplBufAppend(&out, src + openIdx, closeIdx + 2 - openIdx)) {
				tplBufFree(&out);
				return TPL_NULL;
			}
		} else if (ctx != TPL_NULL) {
			Value val;
			if (tplLookupField(ctx, keyBuf, &val)) {
				if (!tplAppendValueEscaped(&out, val)) {
					tplBufFree(&out);
					return TPL_NULL;
				}
			}
		}
		cursor = closeIdx + 2;
	}

	p = out.data;
	out.data = TPL_NULL;
	return p;
}

/* replaceLoopVar then interpolate — matches Lux renderItemList piece. */
static char*
tplRenderLoopPiece(const char* inner, const char* varName, Value elem, ObjInstance* ctx)
{
	char* replaced;
	char* piece;
	replaced = tplInterpolate(inner, TPL_NULL, varName, elem);
	if (replaced == TPL_NULL) return TPL_NULL;
	piece = tplInterpolate(replaced, ctx, TPL_NULL, NIL_VAL);
	free(replaced);
	return piece;
}

static char*
tplRenderFors(const char* srcIn, ObjInstance* ctx)
{
	char* src;
	int cursor;
	TplBuf rebuilt;

	src = tplDupStr(srcIn, -1);
	if (src == TPL_NULL) return TPL_NULL;

	cursor = 0;
	while (1) {
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

		srcLen = (int)strlen(src);
		p = strstr(src + cursor, "{% for ");
		if (p == TPL_NULL) break;
		forStart = (int)(p - src);
		p = strstr(src + forStart, "%}");
		if (p == TPL_NULL) break;
		forTagEnd = (int)(p - src);

		exprLen = forTagEnd - (forStart + (int)strlen("{% for "));
		if (exprLen < 0) exprLen = 0;
		forExpr = tplDupStr(src + forStart + (int)strlen("{% for "), exprLen);
		if (forExpr == TPL_NULL) {
			free(src);
			return TPL_NULL;
		}
		p = strstr(forExpr, " in ");
		if (p == TPL_NULL) {
			free(forExpr);
			cursor = forTagEnd + 2;
			continue;
		}
		inIdx = (int)(p - forExpr);
		if (inIdx >= (int)sizeof(varName)) inIdx = (int)sizeof(varName) - 1;
		memcpy(varName, forExpr, (size_t)inIdx);
		varName[inIdx] = '\0';
		tplTrimInPlace(varName);
		{
			char* rest = forExpr + inIdx + (int)strlen(" in ");
			int rlen = (int)strlen(rest);
			if (rlen >= (int)sizeof(iterableKey)) rlen = (int)sizeof(iterableKey) - 1;
			memcpy(iterableKey, rest, (size_t)rlen);
			iterableKey[rlen] = '\0';
			tplTrimInPlace(iterableKey);
		}
		free(forExpr);

		p = strstr(src + forTagEnd, "{% endfor %}");
		if (p == TPL_NULL) break;
		endTag = (int)(p - src);
		afterFor = forTagEnd + 2;
		inner = tplDupStr(src + afterFor, endTag - afterFor);
		if (inner == TPL_NULL) {
			free(src);
			return TPL_NULL;
		}

		if (!tplBufInit(&pieceBuf)) {
			free(inner);
			free(src);
			return TPL_NULL;
		}
		if (ctx != TPL_NULL && tplLookupField(ctx, iterableKey, &itemsVal) &&
		    IS_ARRAY(itemsVal)) {
			arr = AS_ARRAY(itemsVal);
			for (i = 0; i < arr->count; i++) {
				char* piece = tplRenderLoopPiece(inner, varName, arr->elements[i], ctx);
				if (piece == TPL_NULL) {
					tplBufFree(&pieceBuf);
					free(inner);
					free(src);
					return TPL_NULL;
				}
				if (!tplBufAppend(&pieceBuf, piece, -1)) {
					free(piece);
					tplBufFree(&pieceBuf);
					free(inner);
					free(src);
					return TPL_NULL;
				}
				free(piece);
			}
		}
		free(inner);
		rendered = pieceBuf.data;
		pieceBuf.data = TPL_NULL;

		endforLen = (int)strlen("{% endfor %}");
		if (!tplBufInit(&rebuilt)) {
			free(rendered);
			free(src);
			return TPL_NULL;
		}
		if (!tplBufAppend(&rebuilt, src, forStart) ||
		    !tplBufAppend(&rebuilt, rendered != TPL_NULL ? rendered : "", -1) ||
		    !tplBufAppend(&rebuilt, src + endTag + endforLen, -1)) {
			free(rendered);
			tplBufFree(&rebuilt);
			free(src);
			return TPL_NULL;
		}
		{
			int suffixLen = srcLen - (endTag + endforLen);
			int renderedLen = rebuilt.len - forStart - suffixLen;
			cursor = forStart + renderedLen;
		}
		free(rendered);
		nextSrc = rebuilt.data;
		rebuilt.data = TPL_NULL;
		free(src);
		src = nextSrc;
	}

	return src;
}

static char*
tplRenderFull(const char* path, ObjInstance* ctx)
{
	char* expanded;
	char* withFors;
	char* final;

	expanded = tplExpandIncludesDepth(path, 0);
	if (expanded == TPL_NULL) return TPL_NULL;
	withFors = tplRenderFors(expanded, ctx);
	free(expanded);
	if (withFors == TPL_NULL) return TPL_NULL;
	final = tplInterpolate(withFors, ctx, TPL_NULL, NIL_VAL);
	free(withFors);
	return final;
}
