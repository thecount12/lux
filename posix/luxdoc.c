/*
 * luxdoc - Generate callable documentation from vm.c native table and inline comments.
 *
 * Usage: luxdoc [options] [vm.c]
 *   -o <file>    Write output to file (default: stdout)
 *   -f markdown  Output format: markdown (default)
 *   -f json      Output format: JSON
 *   --lux <dir>  Optionally scan .lux files for comments above fun/class (future)
 *
 * Reads posix/vm.c (or specified path), extracts:
 *   1. kNativeDocs entries (name, signature, description)
 *   2. defineNative("name", ...) for full native list
 *   3. Inline "name(args) -> return" comments above *Native functions
 *
 * Merges sources: kNativeDocs preferred, inline comments fill gaps.
 */

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DOCS 128
#define MAX_NAME 64
#define MAX_SIG 256
#define MAX_DESC 512

typedef struct {
	char name[MAX_NAME];
	char signature[MAX_SIG];
	char description[MAX_DESC];
	bool from_table;
} DocEntry;

static DocEntry docs[MAX_DOCS];
static int doc_count = 0;

static void die(const char* msg) {
	fprintf(stderr, "luxdoc: %s\n", msg);
	exit(1);
}

static char* readFile(const char* path, size_t* out_len) {
	FILE* f = fopen(path, "rb");
	if (!f) die("cannot open file");

	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		die("cannot seek file");
	}
	long sz = ftell(f);
	if (sz < 0) {
		fclose(f);
		die("cannot get file size");
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
		die("cannot read file");
	}
	data[n] = '\0';
	*out_len = n;
	return data;
}

static const char* callableCategory(const char* name) {
	if (strcmp(name, "help") == 0 || strcmp(name, "clock") == 0 || strcmp(name, "epoch") == 0 ||
	    strcmp(name, "floor") == 0 || strcmp(name, "abs") == 0 || strcmp(name, "ceil") == 0 ||
	    strcmp(name, "sqrt") == 0 || strcmp(name, "pow") == 0 || strcmp(name, "log") == 0 ||
	    strcmp(name, "sin") == 0 || strcmp(name, "cos") == 0 || strcmp(name, "assert") == 0) return "Core";
	if (strcmp(name, "readFile") == 0 || strcmp(name, "writeFile") == 0 ||
	    strcmp(name, "appendFile") == 0 || strcmp(name, "deleteFile") == 0 ||
	    strcmp(name, "fileExists") == 0 || strcmp(name, "createDir") == 0 ||
	    strcmp(name, "listDir") == 0 || strcmp(name, "run") == 0) return "File and Directory";
	if (strcmp(name, "len") == 0 || strcmp(name, "strFind") == 0 ||
	    strcmp(name, "strSlice") == 0 || strcmp(name, "strStartsWithAt") == 0 ||
	    strcmp(name, "strTrim") == 0 || strcmp(name, "strSplit") == 0 ||
	    strcmp(name, "arrayIndexOf") == 0 || strcmp(name, "arrayContains") == 0 ||
	    strcmp(name, "arraySort") == 0 || strcmp(name, "arrayBinarySearch") == 0) return "String and Array";
	if (strcmp(name, "parseJSON") == 0 || strcmp(name, "toJSON") == 0 ||
	    strcmp(name, "parseXml") == 0 || strcmp(name, "parseCSV") == 0 ||
	    strcmp(name, "getField") == 0) return "Data Formats";
	if (strncmp(name, "float64_", 8) == 0) return "Float64";
	if (strcmp(name, "httpGet") == 0 || strcmp(name, "httpPost") == 0 ||
	    strcmp(name, "httpPut") == 0 || strcmp(name, "httpRequest") == 0 ||
	    strcmp(name, "httpPostForm") == 0 ||
	    strcmp(name, "httpServer") == 0 || strcmp(name, "Server") == 0) return "HTTP";
	if (strcmp(name, "sha256") == 0 || strcmp(name, "hmacSha256") == 0) return "Crypto";
	if (strcmp(name, "awsSignRequest") == 0 || strcmp(name, "getAwsTimestamp") == 0 ||
	    strcmp(name, "s3ListObjects") == 0 || strcmp(name, "s3GetObject") == 0 ||
	    strcmp(name, "s3PutObject") == 0) return "AWS";
	if (strcmp(name, "dbConnect") == 0 || strcmp(name, "dbQuery") == 0 ||
	    strcmp(name, "dbClose") == 0) return "Database";
	return "User or Other";
}

static DocEntry* findDoc(const char* name) {
	for (int i = 0; i < doc_count; i++) {
		if (strcmp(docs[i].name, name) == 0)
			return &docs[i];
	}
	return NULL;
}

/* Only allow valid C-style identifiers (no spaces) - filters bogus parses */
static bool isValidName(const char* name) {
	if (!name || !name[0]) return false;
	for (const char* p = name; *p; p++) {
		if (*p != '_' && *p != '.' && !isalnum((unsigned char)*p))
			return false;
	}
	return true;
}

static void addOrUpdateDoc(const char* name, const char* sig, const char* desc, bool from_table) {
	/* Trim leading/trailing whitespace from name */
	char trimmed[MAX_NAME];
	size_t i = 0;
	while (name[i] && (name[i] == ' ' || name[i] == '\t')) i++;
	size_t j = 0;
	while (name[i] && j < MAX_NAME - 1) trimmed[j++] = name[i++];
	trimmed[j] = '\0';
	while (j > 0 && (trimmed[j-1] == ' ' || trimmed[j-1] == '\t')) trimmed[--j] = '\0';
	if (!isValidName(trimmed)) return;
	DocEntry* existing = findDoc(trimmed);
	if (existing) {
		if (from_table || (!existing->from_table && sig[0] != '\0')) {
			strncpy(existing->signature, sig, MAX_SIG - 1);
			existing->signature[MAX_SIG - 1] = '\0';
			strncpy(existing->description, desc, MAX_DESC - 1);
			existing->description[MAX_DESC - 1] = '\0';
			existing->from_table = from_table;
		}
		return;
	}
	if (doc_count >= MAX_DOCS) die("too many docs");
	DocEntry* d = &docs[doc_count++];
	strncpy(d->name, trimmed, MAX_NAME - 1);
	d->name[MAX_NAME - 1] = '\0';
	strncpy(d->signature, sig, MAX_SIG - 1);
	d->signature[MAX_SIG - 1] = '\0';
	strncpy(d->description, desc, MAX_DESC - 1);
	d->description[MAX_DESC - 1] = '\0';
	d->from_table = from_table;
}

/* Extract quoted string at *p, advance p past it. Returns static buffer. */
static char extract_buf[1024];
static const char* extractQuoted(const char** p) {
	const char* s = *p;
	if (*s != '"') return NULL;
	s++;
	char* out = extract_buf;
	size_t cap = sizeof(extract_buf) - 1;
	size_t i = 0;
	while (*s && *s != '"' && i < cap) {
		if (*s == '\\' && s[1]) { s++; }
		out[i++] = *s++;
	}
	out[i] = '\0';
	*p = (*s == '"') ? s + 1 : s;
	return extract_buf;
}

/* Parse kNativeDocs array: {"name", "signature", "description"}, */
static void parseNativeDocs(const char* src, size_t len) {
	(void)len;
	const char* kn = strstr(src, "kNativeDocs");
	if (!kn) return;
	/* Find the array's opening { (not a nested one) */
	const char* arrOpen = strstr(kn, "= {");
	if (!arrOpen) return;
	const char* blockStart = arrOpen + 2; /* past "= {" */

	const char* blockEnd = strstr(blockStart, "};");
	if (!blockEnd) return;

	const char* p = blockStart;
	const char* end = blockEnd;

	while (p < end) {
		p = (const char*)memchr(p, '{', end - p);
		if (!p) break;
		p++;
		if (p >= end || *p != '"') continue;

		const char* name = extractQuoted(&p);
		if (!name || !p || p >= end) continue;
		while (p < end && (*p == ' ' || *p == '\t' || *p == ',')) p++;
		if (p >= end || *p != '"') continue;

		const char* sig = extractQuoted(&p);
		if (!sig || !p || p >= end) continue;
		while (p < end && (*p == ' ' || *p == '\t' || *p == ',')) p++;
		if (p >= end || *p != '"') continue;

		const char* desc = extractQuoted(&p);
		if (!desc || !p) continue;

		addOrUpdateDoc(name, sig, desc, true);
	}
}

/* Parse defineNative("name", ...) */
static void parseDefineNative(const char* src, size_t len) {
	const char* p = src;
	const char* end = src + len;
	const char* needle = "defineNative(";

	while (p < end) {
		p = strstr(p, needle);
		if (!p) break;
		p += strlen(needle); /* now p points to opening " of name */
		const char* name = extractQuoted(&p);
		if (name && name[0])
			addOrUpdateDoc(name, "", "", false);
	}
}

/* Parse inline comment before static Value XNative( - format: name(args) -> return */
static void parseInlineComments(const char* src, size_t len) {
	const char* p = src;
	const char* end = src + len;

	while (p < end) {
		const char* block = strstr(p, "/*");
		if (!block) break;
		const char* blockEnd = strstr(block + 2, "*/");
		if (!blockEnd) break;

		const char* afterComment = blockEnd + 2;
		while (afterComment < end && (isspace((unsigned char)*afterComment) || *afterComment == '\n'))
			afterComment++;

		if (strncmp(afterComment, "static Value ", 12) != 0) {
			p = blockEnd + 2;
			continue;
		}
		const char* fnStart = afterComment + 12;
		const char* paren = strchr(fnStart, '(');
		if (!paren || paren - fnStart < 4) {
			p = blockEnd + 2;
			continue;
		}
		size_t nameLen = paren - fnStart;
		if (nameLen >= MAX_NAME || nameLen < 4) {
			p = blockEnd + 2;
			continue;
		}
		if (strncmp(fnStart + nameLen - 6, "Native", 6) != 0) {
			p = blockEnd + 2;
			continue;
		}
		char cName[MAX_NAME];
		memcpy(cName, fnStart, nameLen - 6);
		cName[nameLen - 6] = '\0';

		/* Extract first line of comment as signature/description */
		const char* content = block + 2;
		const char* lineEnd = content;
		while (lineEnd < blockEnd && *lineEnd != '\n') lineEnd++;
		size_t lineLen = lineEnd - content;
		if (lineLen >= MAX_SIG) lineLen = MAX_SIG - 1;
		char line[MAX_SIG];
		memcpy(line, content, lineLen);
		line[lineLen] = '\0';
		/* Trim trailing * and spaces */
		while (lineLen > 0 && (line[lineLen-1] == '*' || line[lineLen-1] == ' ' || line[lineLen-1] == '\t'))
			line[--lineLen] = '\0';

		/* If line looks like "name(args) -> return", use it as signature */
		char sig[MAX_SIG] = "";
		char desc[MAX_DESC] = "";
		if (strstr(line, cName) && strchr(line, '(')) {
			const char* arrow = strstr(line, "->");
			if (arrow) {
				size_t sigLen = arrow - line;
				while (sigLen > 0 && (line[sigLen-1] == ' ' || line[sigLen-1] == '\t')) sigLen--;
				if (sigLen < MAX_SIG) {
					memcpy(sig, line, sigLen);
					sig[sigLen] = '\0';
				}
				const char* rest = arrow + 2;
				while (*rest == ' ' || *rest == '\t') rest++;
				size_t restLen = strlen(rest);
				if (restLen >= MAX_DESC) restLen = MAX_DESC - 1;
				memcpy(desc, rest, restLen);
				desc[restLen] = '\0';
			} else {
				strncpy(sig, line, MAX_SIG - 1);
				sig[MAX_SIG - 1] = '\0';
			}
		} else {
			strncpy(desc, line, MAX_DESC - 1);
			desc[MAX_DESC - 1] = '\0';
		}

		DocEntry* existing = findDoc(cName);
		if (!existing || !existing->from_table) {
			addOrUpdateDoc(cName, sig, desc, false);
		}

		p = blockEnd + 2;
	}
}

static int docCmp(const void* a, const void* b) {
	const DocEntry* da = (const DocEntry*)a;
	const DocEntry* db = (const DocEntry*)b;
	int catCmp = strcmp(callableCategory(da->name), callableCategory(db->name));
	if (catCmp != 0) return catCmp;
	return strcmp(da->name, db->name);
}

static void emitMarkdown(FILE* out) {
	const char* categories[] = {
		"Core", "File and Directory", "String and Array", "Data Formats",
		"HTTP", "Crypto", "AWS", "Database", "User or Other"
	};
	int ncat = (int)(sizeof(categories) / sizeof(categories[0]));

	fprintf(out, "# Lux Built-in Callables\n\n");
	fprintf(out, "Generated by `luxdoc`. Use `help()` or `help(\"name\")` in the REPL for interactive docs.\n\n");

	for (int c = 0; c < ncat; c++) {
		bool any = false;
		for (int i = 0; i < doc_count; i++) {
			if (strcmp(callableCategory(docs[i].name), categories[c]) != 0) continue;
			if (!any) {
				fprintf(out, "## %s\n\n", categories[c]);
				any = true;
			}
			fprintf(out, "### %s\n\n", docs[i].name);
			if (docs[i].signature[0])
				fprintf(out, "```\n%s\n```\n\n", docs[i].signature);
			if (docs[i].description[0])
				fprintf(out, "%s\n\n", docs[i].description);
		}
		if (any) fprintf(out, "---\n\n");
	}
}

static void emitJson(FILE* out) {
	fprintf(out, "{\n  \"callables\": [\n");
	for (int i = 0; i < doc_count; i++) {
		fprintf(out, "    {\n");
		fprintf(out, "      \"name\": \"%s\",\n", docs[i].name);
		fprintf(out, "      \"category\": \"%s\",\n", callableCategory(docs[i].name));
		fprintf(out, "      \"signature\": \"%s\",\n", docs[i].signature);
		fprintf(out, "      \"description\": \"%s\"\n", docs[i].description);
		fprintf(out, "    }%s\n", i < doc_count - 1 ? "," : "");
	}
	fprintf(out, "  ]\n}\n");
}

int main(int argc, char** argv) {
	const char* vmPath = NULL;
	const char* outPath = NULL;
	const char* format = "markdown";

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-o") == 0) {
			if (i + 1 >= argc) die("-o requires argument");
			outPath = argv[++i];
		} else if (strcmp(argv[i], "-f") == 0) {
			if (i + 1 >= argc) die("-f requires argument");
			format = argv[++i];
		} else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			printf("Usage: luxdoc [options] [vm.c]\n");
			printf("  -o <file>   Write output to file (default: stdout)\n");
			printf("  -f <fmt>    Format: markdown, json (default: markdown)\n");
			printf("  -h, --help  Show this help\n");
			printf("\nReads vm.c, extracts kNativeDocs, defineNative, and inline comments.\n");
			return 0;
		} else if (argv[i][0] != '-') {
			vmPath = argv[i];
		}
	}

	if (!vmPath) {
		vmPath = "vm.c";
	}

	size_t len = 0;
	char* src = readFile(vmPath, &len);

	parseNativeDocs(src, len);
	parseDefineNative(src, len);
	parseInlineComments(src, len);

	free(src);

	if (doc_count == 0) {
		fprintf(stderr, "luxdoc: no callables found in %s\n", vmPath);
		return 1;
	}

	qsort(docs, doc_count, sizeof(DocEntry), docCmp);

	FILE* out = stdout;
	if (outPath) {
		out = fopen(outPath, "w");
		if (!out) die("cannot open output file");
	}

	if (strcmp(format, "json") == 0)
		emitJson(out);
	else
		emitMarkdown(out);

	if (outPath)
		fclose(out);

	return 0;
}
