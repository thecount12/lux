#include "lux.h"
#include "markdown.h"

/*
 * Basic Markdown to HTML.
 * Headings (# through ######), paragraphs, emphasis, strong,
 * links, unordered and ordered lists, fenced code, inline code.
 * Text is HTML-escaped. javascript: links become plain text.
 */

typedef struct MdBuf {
	char* data;
	int len;
	int cap;
} MdBuf;

static int
mdBufInit(MdBuf* b)
{
	b->cap = 256;
	b->len = 0;
	b->data = malloc(b->cap);
	if (b->data == nil) return 0;
	b->data[0] = '\0';
	return 1;
}

static void
mdBufFree(MdBuf* b)
{
	if (b->data != nil) free(b->data);
	b->data = nil;
	b->len = 0;
	b->cap = 0;
}

static int
mdBufGrow(MdBuf* b, int need)
{
	char* nd;
	int ncap;

	if (b->len + need + 1 <= b->cap) return 1;
	ncap = b->cap * 2;
	if (ncap < b->len + need + 1) ncap = b->len + need + 1;
	nd = realloc(b->data, ncap);
	if (nd == nil) return 0;
	b->data = nd;
	b->cap = ncap;
	return 1;
}

static int
mdBufAppend(MdBuf* b, char* s, int n)
{
	if (s == nil) return 1;
	if (n < 0) n = strlen(s);
	if (!mdBufGrow(b, n)) return 0;
	memcpy(b->data + b->len, s, n);
	b->len += n;
	b->data[b->len] = '\0';
	return 1;
}

static int
mdBufAppendEscaped(MdBuf* b, char* s, int n)
{
	int i;
	char c;

	if (s == nil) return 1;
	if (n < 0) n = strlen(s);
	for (i = 0; i < n; i++) {
		c = s[i];
		if (c == '&') {
			if (!mdBufAppend(b, "&amp;", 5)) return 0;
		} else if (c == '<') {
			if (!mdBufAppend(b, "&lt;", 4)) return 0;
		} else if (c == '>') {
			if (!mdBufAppend(b, "&gt;", 4)) return 0;
		} else if (c == '"') {
			if (!mdBufAppend(b, "&quot;", 6)) return 0;
		} else if (c == '\'') {
			if (!mdBufAppend(b, "&#39;", 5)) return 0;
		} else {
			if (!mdBufAppend(b, &c, 1)) return 0;
		}
	}
	return 1;
}

static int
mdIsDigit(char c)
{
	return c >= '0' && c <= '9';
}

static int
mdCiStartsWith(char* s, int n, char* pref)
{
	int i, plen;

	plen = strlen(pref);
	if (n < plen) return 0;
	for (i = 0; i < plen; i++) {
		char a = s[i];
		char b = pref[i];
		if (a >= 'A' && a <= 'Z') a = a - 'A' + 'a';
		if (b >= 'A' && b <= 'Z') b = b - 'A' + 'a';
		if (a != b) return 0;
	}
	return 1;
}

static int
mdJsUrl(char* url, int n)
{
	return mdCiStartsWith(url, n, "javascript:");
}

/* Find closing delim of len delimLen starting at i; returns end index or -1. */
static int
mdFindClose(char* s, int len, int i, char delim, int delimLen)
{
	int j;

	for (j = i; j + delimLen <= len; j++) {
		int k;
		int ok = 1;
		for (k = 0; k < delimLen; k++) {
			if (s[j + k] != delim) {
				ok = 0;
				break;
			}
		}
		if (ok) return j;
	}
	return -1;
}

static int
mdRenderInline(MdBuf* out, char* s, int len)
{
	int i;

	i = 0;
	while (i < len) {
		if (s[i] == '`' ) {
			int close = mdFindClose(s, len, i + 1, '`', 1);
			if (close >= 0) {
				if (!mdBufAppend(out, "<code>", 6)) return 0;
				if (!mdBufAppendEscaped(out, s + i + 1, close - (i + 1))) return 0;
				if (!mdBufAppend(out, "</code>", 7)) return 0;
				i = close + 1;
				continue;
			}
		}
		if (s[i] == '[') {
			int textEnd, urlStart, urlEnd;
			textEnd = mdFindClose(s, len, i + 1, ']', 1);
			if (textEnd >= 0 && textEnd + 1 < len && s[textEnd + 1] == '(') {
				urlStart = textEnd + 2;
				urlEnd = mdFindClose(s, len, urlStart, ')', 1);
				if (urlEnd >= 0) {
					char* url = s + urlStart;
					int urlLen = urlEnd - urlStart;
					if (mdJsUrl(url, urlLen)) {
						if (!mdBufAppendEscaped(out, s + i + 1, textEnd - (i + 1))) return 0;
					} else {
						if (!mdBufAppend(out, "<a href=\"", 9)) return 0;
						if (!mdBufAppendEscaped(out, url, urlLen)) return 0;
						if (!mdBufAppend(out, "\">", 2)) return 0;
						if (!mdBufAppendEscaped(out, s + i + 1, textEnd - (i + 1))) return 0;
						if (!mdBufAppend(out, "</a>", 4)) return 0;
					}
					i = urlEnd + 1;
					continue;
				}
			}
		}
		if (s[i] == '*' && i + 1 < len && s[i + 1] == '*') {
			int close = mdFindClose(s, len, i + 2, '*', 2);
			if (close >= 0) {
				if (!mdBufAppend(out, "<strong>", 8)) return 0;
				if (!mdRenderInline(out, s + i + 2, close - (i + 2))) return 0;
				if (!mdBufAppend(out, "</strong>", 9)) return 0;
				i = close + 2;
				continue;
			}
		}
		if (s[i] == '*') {
			int close = mdFindClose(s, len, i + 1, '*', 1);
			if (close >= 0 && !(close + 1 < len && s[close + 1] == '*')) {
				if (!mdBufAppend(out, "<em>", 4)) return 0;
				if (!mdRenderInline(out, s + i + 1, close - (i + 1))) return 0;
				if (!mdBufAppend(out, "</em>", 5)) return 0;
				i = close + 1;
				continue;
			}
		}
		if (!mdBufAppendEscaped(out, s + i, 1)) return 0;
		i++;
	}
	return 1;
}

static int
mdLineEnd(char* src, int len, int i)
{
	while (i < len && src[i] != '\n') i++;
	return i;
}

static int
mdIsBlankLine(char* line, int n)
{
	int i;
	for (i = 0; i < n; i++) {
		if (line[i] != ' ' && line[i] != '\t' && line[i] != '\r') return 0;
	}
	return 1;
}

static int
mdHeadingLevel(char* line, int n, int* contentStart)
{
	int i, level;

	level = 0;
	for (i = 0; i < n && i < 6 && line[i] == '#'; i++)
		level++;
	if (level == 0) return 0;
	if (i >= n || (line[i] != ' ' && line[i] != '\t')) return 0;
	while (i < n && (line[i] == ' ' || line[i] == '\t')) i++;
	*contentStart = i;
	return level;
}

static int
mdIsUlItem(char* line, int n, int* contentStart)
{
	int i = 0;
	while (i < n && (line[i] == ' ' || line[i] == '\t')) i++;
	if (i >= n) return 0;
	if (line[i] != '-' && line[i] != '*' && line[i] != '+') return 0;
	if (i + 1 >= n || (line[i + 1] != ' ' && line[i + 1] != '\t')) return 0;
	i += 2;
	while (i < n && (line[i] == ' ' || line[i] == '\t')) i++;
	*contentStart = i;
	return 1;
}

static int
mdIsOlItem(char* line, int n, int* contentStart)
{
	int i = 0;
	while (i < n && (line[i] == ' ' || line[i] == '\t')) i++;
	if (i >= n || !mdIsDigit(line[i])) return 0;
	while (i < n && mdIsDigit(line[i])) i++;
	if (i >= n || line[i] != '.') return 0;
	if (i + 1 >= n || (line[i + 1] != ' ' && line[i + 1] != '\t')) return 0;
	i += 2;
	while (i < n && (line[i] == ' ' || line[i] == '\t')) i++;
	*contentStart = i;
	return 1;
}

static int
mdIsFence(char* line, int n)
{
	int i = 0;
	int ticks = 0;
	while (i < n && (line[i] == ' ' || line[i] == '\t')) i++;
	while (i < n && line[i] == '`') {
		ticks++;
		i++;
	}
	if (ticks < 3) return 0;
	return 1;
}

static char*
mdLoadFile(char* path, int* outLen)
{
	int fd;
	Dir* d;
	char* buf;
	int n, len;

	*outLen = 0;
	fd = open(path, OREAD);
	if (fd < 0) return nil;
	d = dirfstat(fd);
	if (d == nil) {
		close(fd);
		return nil;
	}
	len = (int)d->length;
	free(d);
	buf = malloc(len + 1);
	if (buf == nil) {
		close(fd);
		return nil;
	}
	n = read(fd, buf, len);
	close(fd);
	if (n < 0) {
		free(buf);
		return nil;
	}
	buf[n] = '\0';
	*outLen = n;
	return buf;
}

char*
mdToHtml(char* src, int len)
{
	MdBuf out;
	int i;
	char tag[16];

	if (src == nil) return nil;
	if (len < 0) len = strlen(src);
	if (!mdBufInit(&out)) return nil;

	i = 0;
	while (i < len) {
		int lineStart, lineEnd, lineLen, cs;
		char* line;
		int level;

		/* skip blank lines between blocks */
		lineStart = i;
		lineEnd = mdLineEnd(src, len, i);
		lineLen = lineEnd - lineStart;
		if (lineLen > 0 && src[lineEnd - 1] == '\r') lineLen--;
		line = src + lineStart;

		if (mdIsBlankLine(line, lineLen)) {
			i = lineEnd < len ? lineEnd + 1 : len;
			continue;
		}

		/* fenced code */
		if (mdIsFence(line, lineLen)) {
			i = lineEnd < len ? lineEnd + 1 : len;
			if (!mdBufAppend(&out, "<pre><code>", 11)) {
				mdBufFree(&out);
				return nil;
			}
			while (i < len) {
				int ls, le, ll;
				ls = i;
				le = mdLineEnd(src, len, i);
				ll = le - ls;
				if (ll > 0 && src[le - 1] == '\r') ll--;
				if (mdIsFence(src + ls, ll)) {
					i = le < len ? le + 1 : len;
					break;
				}
				if (!mdBufAppendEscaped(&out, src + ls, ll)) {
					mdBufFree(&out);
					return nil;
				}
				if (!mdBufAppend(&out, "\n", 1)) {
					mdBufFree(&out);
					return nil;
				}
				i = le < len ? le + 1 : len;
			}
			if (!mdBufAppend(&out, "</code></pre>\n", 14)) {
				mdBufFree(&out);
				return nil;
			}
			continue;
		}

		/* heading */
		level = mdHeadingLevel(line, lineLen, &cs);
		if (level > 0) {
			snprint(tag, sizeof(tag), "<h%d>", level);
			if (!mdBufAppend(&out, tag, -1)) {
				mdBufFree(&out);
				return nil;
			}
			if (!mdRenderInline(&out, line + cs, lineLen - cs)) {
				mdBufFree(&out);
				return nil;
			}
			snprint(tag, sizeof(tag), "</h%d>\n", level);
			if (!mdBufAppend(&out, tag, -1)) {
				mdBufFree(&out);
				return nil;
			}
			i = lineEnd < len ? lineEnd + 1 : len;
			continue;
		}

		/* unordered list */
		if (mdIsUlItem(line, lineLen, &cs)) {
			if (!mdBufAppend(&out, "<ul>\n", 5)) {
				mdBufFree(&out);
				return nil;
			}
			while (i < len) {
				int ls, le, ll, ics;
				ls = i;
				le = mdLineEnd(src, len, i);
				ll = le - ls;
				if (ll > 0 && src[le - 1] == '\r') ll--;
				if (mdIsBlankLine(src + ls, ll)) break;
				if (!mdIsUlItem(src + ls, ll, &ics)) break;
				if (!mdBufAppend(&out, "<li>", 4)) {
					mdBufFree(&out);
					return nil;
				}
				if (!mdRenderInline(&out, src + ls + ics, ll - ics)) {
					mdBufFree(&out);
					return nil;
				}
				if (!mdBufAppend(&out, "</li>\n", 6)) {
					mdBufFree(&out);
					return nil;
				}
				i = le < len ? le + 1 : len;
			}
			if (!mdBufAppend(&out, "</ul>\n", 6)) {
				mdBufFree(&out);
				return nil;
			}
			continue;
		}

		/* ordered list */
		if (mdIsOlItem(line, lineLen, &cs)) {
			if (!mdBufAppend(&out, "<ol>\n", 5)) {
				mdBufFree(&out);
				return nil;
			}
			while (i < len) {
				int ls, le, ll, ics;
				ls = i;
				le = mdLineEnd(src, len, i);
				ll = le - ls;
				if (ll > 0 && src[le - 1] == '\r') ll--;
				if (mdIsBlankLine(src + ls, ll)) break;
				if (!mdIsOlItem(src + ls, ll, &ics)) break;
				if (!mdBufAppend(&out, "<li>", 4)) {
					mdBufFree(&out);
					return nil;
				}
				if (!mdRenderInline(&out, src + ls + ics, ll - ics)) {
					mdBufFree(&out);
					return nil;
				}
				if (!mdBufAppend(&out, "</li>\n", 6)) {
					mdBufFree(&out);
					return nil;
				}
				i = le < len ? le + 1 : len;
			}
			if (!mdBufAppend(&out, "</ol>\n", 6)) {
				mdBufFree(&out);
				return nil;
			}
			continue;
		}

		/* paragraph: consecutive non-blank non-special lines */
		if (!mdBufAppend(&out, "<p>", 3)) {
			mdBufFree(&out);
			return nil;
		}
		{
			int first = 1;
			while (i < len) {
				int ls, le, ll, dummy;
				ls = i;
				le = mdLineEnd(src, len, i);
				ll = le - ls;
				if (ll > 0 && src[le - 1] == '\r') ll--;
				if (mdIsBlankLine(src + ls, ll)) break;
				if (mdIsFence(src + ls, ll)) break;
				if (mdHeadingLevel(src + ls, ll, &dummy) > 0) break;
				if (mdIsUlItem(src + ls, ll, &dummy)) break;
				if (mdIsOlItem(src + ls, ll, &dummy)) break;
				if (!first) {
					if (!mdBufAppend(&out, " ", 1)) {
						mdBufFree(&out);
						return nil;
					}
				}
				first = 0;
				if (!mdRenderInline(&out, src + ls, ll)) {
					mdBufFree(&out);
					return nil;
				}
				i = le < len ? le + 1 : len;
			}
		}
		if (!mdBufAppend(&out, "</p>\n", 5)) {
			mdBufFree(&out);
			return nil;
		}
	}

	return out.data;
}

char*
mdRenderFile(char* path)
{
	char* src;
	int len;
	char* html;

	src = mdLoadFile(path, &len);
	if (src == nil) return nil;
	html = mdToHtml(src, len);
	free(src);
	return html;
}
