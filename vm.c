#include "lux.h"
#include "common.h"
#include "value.h"
#include "chunk.h"
#include "object.h"
#include "vm.h"
#include "memory.h"
#include "compiler.h"
#include "debug.h"
#include "table.h"
#include "float64.h"
#include "template_render.h"
#include "markdown.h"
/* Forward declarations for dict natives (avoid pulling types.h into vm.c) */
Value dictInitNative(int argCount, Value* args);
Value dictPutNative(int argCount, Value* args);
Value dictGetNative(int argCount, Value* args);
Value dictHasNative(int argCount, Value* args);
Value dictRemoveNative(int argCount, Value* args);
Value dictSizeNative(int argCount, Value* args);
Value dictClearNative(int argCount, Value* args);
Value dictIterNative(int argCount, Value* args);
Value fileInitNative(int argCount, Value* args);
Value fileReadLineNative(int argCount, Value* args);
Value fileCloseNative(int argCount, Value* args);
void fileSetClass(ObjClass* klass);
Value ninepInitNative(int argCount, Value* args);
Value ninepFileNative(int argCount, Value* args);
Value ninepExportNative(int argCount, Value* args);
Value ninepListenNative(int argCount, Value* args);
Value ninepPostNative(int argCount, Value* args);
Value ninepConnectNative(int argCount, Value* args);
Value ninepReadNative(int argCount, Value* args);
Value ninepWriteNative(int argCount, Value* args);
Value ninepGetNative(int argCount, Value* args);
Value ninepPutNative(int argCount, Value* args);
Value ninepLsNative(int argCount, Value* args);
Value ninepStatNative(int argCount, Value* args);
Value ninepCloseNative(int argCount, Value* args);
void ninepSetClasses(ObjClass* server, ObjClass* conn);
#include <libsec.h>

/* SHA-256 produces 32 bytes */
#ifndef SHA2_256dlen
#define SHA2_256dlen 32
#endif

VM vm;

typedef struct {
	const char* name;
	const char* signature;
	const char* description;
} NativeDoc;

static const NativeDoc kNativeDocs[] = {
	{"clock", "clock()", "Return process uptime in seconds."},
	{"epoch", "epoch()", "Return Unix epoch time in seconds (UTC)."},
	{"exit", "exit([code])", "Terminate the Lux process with an optional numeric status (default 0)."},
	{"args", "args()", "Return command-line arguments passed after the script path."},
	{"floor", "floor(number)", "Return largest integer less than or equal to number."},
	{"abs", "abs(number)", "Return the absolute value of a number."},
	{"ceil", "ceil(number)", "Return smallest integer greater than or equal to number."},
	{"sqrt", "sqrt(number)", "Return the square root of a number."},
	{"pow", "pow(base, exp)", "Return base raised to exp."},
	{"log", "log(number)", "Return the natural logarithm of a number."},
	{"sin", "sin(number)", "Return the sine of a number (radians)."},
	{"cos", "cos(number)", "Return the cosine of a number (radians)."},
	{"readFile", "readFile(path)", "Read a file and return its contents as a string."},
	{"File", "File(path)", "Open a file for line-at-a-time reading. f.ok is false if open failed. f.readLine() returns the next line or nil at EOF. f.close() closes the handle."},
	{"renderTemplate", "renderTemplate(path, ctx)", "Render a .tpl file with {{ }}, {% for %}, {% include %}, {% include_md %} using ctx instance fields."},
	{"markdownToHtml", "markdownToHtml(md)", "Convert Markdown text to an HTML fragment."},
	{"renderMarkdown", "renderMarkdown(path)", "Read a Markdown file and convert it to an HTML fragment."},
	{"writeFile", "writeFile(path, content)", "Write content to a file."},
	{"appendFile", "appendFile(path, content)", "Append content to a file."},
	{"deleteFile", "deleteFile(path)", "Delete a file."},
	{"fileExists", "fileExists(path)", "Return whether a file exists."},
	{"createDir", "createDir(path)", "Create a directory."},
	{"listDir", "listDir(path)", "List directory entries as a newline-separated string."},
	{"run", "run(cmd)", "Run a shell command and return stdout as string, or nil on failure."},
	{"netLookup", "netLookup(host)", "Resolve a hostname to its first IP address string, or nil."},
	{"netPing", "netPing(host, port)", "TCP-connect probe; return round-trip milliseconds, or -1 on failure."},
	{"len", "len(value)", "Return length for strings and arrays."},
	{"strFind", "strFind(haystack, needle)", "Return index of substring or -1."},
	{"strSlice", "strSlice(s, start, end)", "Return substring from start to end."},
	{"strStartsWithAt", "strStartsWithAt(s, prefix, offset)", "Check prefix match at offset."},
	{"strTrim", "strTrim(s)", "Trim surrounding whitespace."},
	{"strSplit", "strSplit(s, delim)", "Split string into an array."},
	{"arrayIndexOf", "arrayIndexOf(arr, value)", "Return index of value in array or -1."},
	{"arrayContains", "arrayContains(arr, value)", "Return whether array contains value."},
	{"arraySort", "arraySort(arr)", "Sort an array in place."},
	{"arrayBinarySearch", "arrayBinarySearch(arr, value)", "Binary-search sorted array."},
	{"float64_available", "float64_available()", "Return whether Float64Array natives are present."},
	{"float64_new", "float64_new(length)", "Create a Float64Array of the given length."},
	{"float64_get", "float64_get(array, index)", "Get a Float64Array element."},
	{"float64_set", "float64_set(array, index, value)", "Set a Float64Array element."},
	{"float64_dot", "float64_dot(a, b)", "Dot product of two Float64Arrays."},
	{"float64_fill", "float64_fill(array, value)", "Fill a Float64Array with a value."},
	{"float64_copy", "float64_copy(array)", "Copy a Float64Array."},
	{"float64_slice", "float64_slice(array, start, end)", "Copy a half-open slice of a Float64Array."},
	{"float64_sum", "float64_sum(array)", "Sum of Float64Array elements."},
	{"float64_mean", "float64_mean(array)", "Mean of Float64Array elements."},
	{"float64_min", "float64_min(array)", "Minimum Float64Array element."},
	{"float64_max", "float64_max(array)", "Maximum Float64Array element."},
	{"float64_add", "float64_add(a, b)", "Elementwise sum; returns a new Float64Array."},
	{"float64_mul", "float64_mul(a, b)", "Elementwise product; returns a new Float64Array."},
	{"float64_axpy", "float64_axpy(a, x, y)", "y[i] += a * x[i] in place; returns y."},
	{"parseJSON", "parseJSON(json)", "Parse JSON text into Lux values."},
	{"toJSON", "toJSON(value)", "Serialize a Lux value to JSON text."},
	{"parseCSV", "parseCSV(text, [sep])", "Parse quoted CSV text into an array of row arrays."},
	{"getField", "getField(obj, name)", "Get an instance field by string name, or nil."},
	{"parseXml", "parseXml(xml)", "Parse XML text into Lux values."},
	{"httpGet", "httpGet(url)", "Make an HTTP GET request."},
	{"httpPost", "httpPost(url, body)", "Make an HTTP POST request with a JSON body."},
	{"httpPut", "httpPut(url, body)", "Make an HTTP PUT request."},
	{"httpRequest", "httpRequest(method, url, body, headers)", "Make a generic HTTP request. Body may be a string, nil, Form, or parts array."},
	{"httpPostForm", "httpPostForm(url, parts, [headers])", "POST multipart/form-data from a parts array or Form instance."},
	{"httpServer", "httpServer(port, handler)", "Start a simple HTTP server."},
	{"Server", "Server(port)", "HTTP server with routing, static files, and .handle() for Lambda."},
	{"NineP", "NineP()", "Create a synthetic 9P file server, or NineP.connect(addr) for a client."},
	{"NinePConn", "NineP.connect(addr)", "9P client: ls, read, write, stat, close. Failed ops set .err."},
	{"sha256", "sha256(text)", "Compute SHA-256 hash."},
	{"hmacSha256", "hmacSha256(key, text)", "Compute HMAC-SHA256."},
	{"typeof", "typeof(value)", "Return the runtime type name or class name for objects."},
	{"awsSignRequest", "awsSignRequest(method, path, query, headers, payloadHash, region, service, accessKey, secretKey)", "Build AWS Signature V4 headers."},
	{"getAwsTimestamp", "getAwsTimestamp()", "Return AWS timestamp fields."},
	{"s3ListObjects", "s3ListObjects(endpoint, bucket, prefix, region, accessKey, secretKey)", "List S3 objects."},
	{"s3GetObject", "s3GetObject(endpoint, bucket, key, region, accessKey, secretKey)", "Read object contents from S3."},
	{"s3PutObject", "s3PutObject(endpoint, bucket, key, body, region, accessKey, secretKey)", "Upload object data to S3."},
	{"help", "help() or help(name)", "List available callables or show details for one name."},
};

static int
compareNamePtr(const void* a, const void* b)
{
	const char* const* lhs = (const char* const*)a;
	const char* const* rhs = (const char* const*)b;
	return strcmp(*lhs, *rhs);
}

static const NativeDoc*
findNativeDoc(const char* name)
{
	int count = (int)(sizeof(kNativeDocs) / sizeof(kNativeDocs[0]));
	for (int i = 0; i < count; i++) {
		if (strcmp(kNativeDocs[i].name, name) == 0)
			return &kNativeDocs[i];
	}
	return nil;
}

static bool
isCallableGlobal(Value value)
{
	if (!IS_OBJ(value))
		return false;

	switch (OBJ_TYPE(value)) {
	case OBJ_NATIVE:
	case OBJ_FUNCTION:
	case OBJ_CLOSURE:
	case OBJ_CLASS:
	case OBJ_BOUND_METHOD:
		return true;
	default:
		return false;
	}
}

static const char*
callableCategory(const char* name)
{
	if (strcmp(name, "help") == 0 || strcmp(name, "clock") == 0 || strcmp(name, "epoch") == 0 || strcmp(name, "typeof") == 0 || strcmp(name, "exit") == 0 || strcmp(name, "args") == 0)
		return "Core";
	if (strcmp(name, "floor") == 0 || strcmp(name, "abs") == 0 || strcmp(name, "ceil") == 0 ||
	    strcmp(name, "sqrt") == 0 || strcmp(name, "pow") == 0 || strcmp(name, "log") == 0 ||
	    strcmp(name, "sin") == 0 || strcmp(name, "cos") == 0)
		return "Math";
	if (strcmp(name, "readFile") == 0 || strcmp(name, "writeFile") == 0 ||
	    strcmp(name, "appendFile") == 0 || strcmp(name, "deleteFile") == 0 ||
	    strcmp(name, "fileExists") == 0 || strcmp(name, "createDir") == 0 ||
	    strcmp(name, "listDir") == 0 || strcmp(name, "run") == 0 ||
	    strcmp(name, "File") == 0 ||
	    strcmp(name, "renderTemplate") == 0 ||
	    strcmp(name, "markdownToHtml") == 0 ||
	    strcmp(name, "renderMarkdown") == 0)
		return "File and Directory";
	if (strcmp(name, "netLookup") == 0 || strcmp(name, "netPing") == 0)
		return "Network";
	if (strcmp(name, "NineP") == 0 || strcmp(name, "NinePConn") == 0)
		return "9P";
	if (strcmp(name, "len") == 0 || strcmp(name, "strFind") == 0 ||
	    strcmp(name, "strSlice") == 0 || strcmp(name, "strStartsWithAt") == 0 ||
	    strcmp(name, "strTrim") == 0 || strcmp(name, "strSplit") == 0 ||
	    strcmp(name, "arrayIndexOf") == 0 || strcmp(name, "arrayContains") == 0 ||
	    strcmp(name, "arraySort") == 0 || strcmp(name, "arrayBinarySearch") == 0)
		return "String and Array";
	if (strcmp(name, "parseJSON") == 0 || strcmp(name, "toJSON") == 0 ||
	    strcmp(name, "parseXml") == 0 || strcmp(name, "parseCSV") == 0 ||
	    strcmp(name, "getField") == 0)
		return "Data Formats";
	if (strncmp(name, "float64_", 8) == 0)
		return "Float64";
	if (strcmp(name, "httpGet") == 0 || strcmp(name, "httpPost") == 0 ||
	    strcmp(name, "httpPut") == 0 || strcmp(name, "httpRequest") == 0 ||
	    strcmp(name, "httpPostForm") == 0 ||
	    strcmp(name, "httpServer") == 0 || strcmp(name, "Server") == 0)
		return "HTTP";
	if (strcmp(name, "sha256") == 0 || strcmp(name, "hmacSha256") == 0)
		return "Crypto";
	if (strcmp(name, "awsSignRequest") == 0 || strcmp(name, "getAwsTimestamp") == 0 ||
	    strcmp(name, "s3ListObjects") == 0 || strcmp(name, "s3GetObject") == 0 ||
	    strcmp(name, "s3PutObject") == 0)
		return "AWS";
	if (strcmp(name, "dbConnect") == 0 || strcmp(name, "dbQuery") == 0 ||
	    strcmp(name, "dbClose") == 0)
		return "Database";
	return "User or Other";
}

static Value
typeLiteral(const char* literal)
{
	int len = 0;
	while (literal[len] != '\0')
		len++;
	return OBJ_VAL(copyString(literal, len));
}

static Value
typeofNative(int argCount, Value* args)
{
	if (argCount != 1)
		return NIL_VAL;

	Value value = args[0];
	if (IS_BOOL(value))
		return typeLiteral("bool");
	if (IS_NIL(value))
		return typeLiteral("nil");
	if (IS_NUMBER(value))
		return typeLiteral("number");
	if (IS_STRING(value))
		return typeLiteral("string");
	if (IS_ARRAY(value))
		return typeLiteral("array");
	if (IS_FLOATARRAY(value))
		return typeLiteral("float64array");
	if (IS_CLASS(value))
		return typeLiteral("class");
	if (IS_CLOSURE(value))
		return typeLiteral("closure");
	if (IS_FUNCTION(value))
		return typeLiteral("function");
	if (IS_NATIVE(value))
		return typeLiteral("native");
	if (IS_BOUND_METHOD(value))
		return typeLiteral("bound_method");
	if (IS_INSTANCE(value)) {
		ObjInstance* inst = AS_INSTANCE(value);
		if (inst->klass != nil && inst->klass->name != nil)
			return OBJ_VAL(copyString(inst->klass->name->chars, inst->klass->name->length));
		return typeLiteral("object");
	}
	if (IS_OBJ(value))
		return typeLiteral("object");
	return typeLiteral("value");
}

static Value
helpNative(int argCount, Value* args)
{
	if (argCount > 1) {
		print("Usage: help() or help(name)\n");
		return NIL_VAL;
	}

	if (argCount == 1) {
		if (!IS_STRING(args[0])) {
			print("help(name) expects a string name.\n");
			return NIL_VAL;
		}

		char* name = AS_CSTRING(args[0]);
		Value found = NIL_VAL;
		bool exists = false;

		for (int i = 0; i < vm.globals.capacity; i++) {
			Entry* entry = &vm.globals.entries[i];
			if (entry->key != nil && strcmp(entry->key->chars, name) == 0) {
				exists = true;
				found = entry->value;
				break;
			}
		}

		if (!exists) {
			print("No global named '%s'.\n", name);
			if (strcmp(name, "class") == 0) {
				print("'class' is a language keyword, not a global value.\n");
				print("Define a class first, then call help(\"ClassName\").\n");
				return NIL_VAL;
			}
			if (name[0] >= 'A' && name[0] <= 'Z') {
				print("Tip: classes only appear in help() after they are defined or imported in the current run.\n");
			}
			return NIL_VAL;
		}

		print("%s\n", name);
		const NativeDoc* doc = findNativeDoc(name);
		if (doc != nil) {
			print("  %s\n", doc->signature);
			print("  %s\n", doc->description);
		}

		if (!IS_OBJ(found)) {
			print("  Type: value\n");
			return NIL_VAL;
		}

		switch (OBJ_TYPE(found)) {
		case OBJ_NATIVE:
			print("  Type: native function\n");
			break;
		case OBJ_FUNCTION:
			print("  Type: function (arity %d)\n", AS_FUNCTION(found)->arity);
			break;
		case OBJ_CLOSURE:
			print("  Type: closure (arity %d)\n", AS_CLOSURE(found)->function->arity);
			break;
		case OBJ_CLASS: {
			ObjClass* klass = AS_CLASS(found);
			print("  Type: class\n");
			int shown = 0;
			for (int i = 0; i < klass->methods.capacity; i++) {
				Entry* method = &klass->methods.entries[i];
				if (method->key == nil)
					continue;
				if (shown == 0)
					print("  Methods:\n");
				print("    %s\n", method->key->chars);
				shown++;
				if (shown >= 20)
					break;
			}
			if (shown == 0)
				print("  Methods: (none)\n");
			break;
		}
		case OBJ_BOUND_METHOD:
			print("  Type: bound method\n");
			break;
		default:
			print("  Type: value\n");
			break;
		}

		return NIL_VAL;
	}

	print("Available global callables:\n");
	print("  help()\n");
	print("  help(\"name\")\n\n");

	int nameCap = 32;
	int nameCount = 0;
	char** names = (char**)malloc(sizeof(char*) * nameCap);
	if (names == nil) {
		print("Out of memory while listing globals.\n");
		return NIL_VAL;
	}

	for (int i = 0; i < vm.globals.capacity; i++) {
		Entry* entry = &vm.globals.entries[i];
		if (entry->key == nil)
			continue;
		if (!isCallableGlobal(entry->value))
			continue;

		if (nameCount >= nameCap) {
			nameCap *= 2;
			char** grown = realloc(names, sizeof(char*) * nameCap);
			if (grown == nil) {
				free(names);
				print("Out of memory while listing globals.\n");
				return NIL_VAL;
			}
			names = grown;
		}

		names[nameCount++] = entry->key->chars;
	}

	qsort(names, nameCount, sizeof(char*), compareNamePtr);
	const char* categories[] = {
		"Core",
		"Math",
		"File and Directory",
		"String and Array",
		"Data Formats",
		"Float64",
		"HTTP",
		"Network",
		"9P",
		"Crypto",
		"AWS",
		"Database",
		"User or Other",
	};
	int categoryCount = (int)(sizeof(categories) / sizeof(categories[0]));
	for (int c = 0; c < categoryCount; c++) {
		bool any = false;
		for (int i = 0; i < nameCount; i++) {
			if (strcmp(callableCategory(names[i]), categories[c]) == 0) {
				if (!any) {
					print("%s:\n", categories[c]);
					any = true;
				}
				print("  %s\n", names[i]);
			}
		}
		if (any)
			print("\n");
	}

	free(names);
	return NIL_VAL;
}

/* Forward declaration for native helpers that report runtime errors before
 * runtimeError() is defined later in this translation unit.
 */
static void runtimeError(char *format, ...);
static void nativeError(char *format, ...);

static Value
assertNative(int argCount, Value* args)
{
	if (argCount < 1 || argCount > 2) {
		vm.nativePanic = 1;
		snprint(vm.nativePanicMsg, sizeof(vm.nativePanicMsg),
		        "assert() expects 1 or 2 arguments, got %d.", argCount);
		return NIL_VAL;
	}
	if (!IS_NIL(args[0]) && !(IS_BOOL(args[0]) && !AS_BOOL(args[0])))
		return BOOL_VAL(1);
	vm.nativePanic = 1;
	if (argCount == 2 && IS_STRING(args[1]))
		snprint(vm.nativePanicMsg, sizeof(vm.nativePanicMsg),
		        "Assertion failed: %s", AS_CSTRING(args[1]));
	else
		snprint(vm.nativePanicMsg, sizeof(vm.nativePanicMsg),
		        "Assertion failed.");
	return NIL_VAL;
}

static Value 
clockNative(int argCount, Value* args)
{
    (void)argCount; (void)args;
    // Plan 9: use nsec() for nanoseconds since boot, divide by 1e9 for seconds
    return NUMBER_VAL((double)nsec() / 1000000000.0);
}

static Value
epochNative(int argCount, Value* args)
{
	(void)argCount;
	(void)args;
	return NUMBER_VAL((double)time(0));
}

static Value
floorNative(int argCount, Value* args)
{
	if (argCount != 1 || !IS_NUMBER(args[0])) {
		return NIL_VAL;
	}
	double value = AS_NUMBER(args[0]);
	/* Integer floor for Plan 9 */
	long result;
	if (value >= 0) {
		result = (long)value;
	} else {
		result = (long)value;
		if ((double)result != value)
			result--;
	}
	return NUMBER_VAL((double)result);
}

/* Quiet NaN / -Inf bit patterns. Plan 9 libc traps on sqrt(-1), log(x<=0),
 * instead of returning IEEE values, so natives must not call those. */
static Value
bitsNumber(uvlong u)
{
	double d;

	memcpy(&d, &u, sizeof d);
	return NUMBER_VAL(d);
}

static Value
nanNumber(void)
{
	return bitsNumber((uvlong)0x7ff8000000000000);
}

static int
doubleIsNaN(double d)
{
	uvlong u;

	memcpy(&u, &d, sizeof u);
	return (u & (uvlong)0x7ff0000000000000) == (uvlong)0x7ff0000000000000
		&& (u & (uvlong)0x000fffffffffffff) != 0;
}

static Value
mathUnaryNative(int argCount, Value* args, double (*fn)(double))
{
	if (argCount != 1 || !IS_NUMBER(args[0]))
		return NIL_VAL;
	return NUMBER_VAL(fn(AS_NUMBER(args[0])));
}

static Value
absNative(int argCount, Value* args)
{
	return mathUnaryNative(argCount, args, fabs);
}

static Value
ceilNative(int argCount, Value* args)
{
	return mathUnaryNative(argCount, args, ceil);
}

static Value
sqrtNative(int argCount, Value* args)
{
	double x;

	if (argCount != 1 || !IS_NUMBER(args[0]))
		return NIL_VAL;
	x = AS_NUMBER(args[0]);
	if (doubleIsNaN(x) || x < 0)
		return nanNumber();
	return NUMBER_VAL(sqrt(x));
}

static Value
logNative(int argCount, Value* args)
{
	double x;

	if (argCount != 1 || !IS_NUMBER(args[0]))
		return NIL_VAL;
	x = AS_NUMBER(args[0]);
	if (doubleIsNaN(x) || x < 0)
		return nanNumber();
	if (x == 0)
		return bitsNumber((uvlong)0xfff0000000000000);	/* -Inf */
	return NUMBER_VAL(log(x));
}

static Value
sinNative(int argCount, Value* args)
{
	return mathUnaryNative(argCount, args, sin);
}

static Value
cosNative(int argCount, Value* args)
{
	return mathUnaryNative(argCount, args, cos);
}

static Value
powNative(int argCount, Value* args)
{
	if (argCount != 2 || !IS_NUMBER(args[0]) || !IS_NUMBER(args[1]))
		return NIL_VAL;
	return NUMBER_VAL(pow(AS_NUMBER(args[0]), AS_NUMBER(args[1])));
}

/* Native function to read a file: readFile(path) -> string */
static Value 
readFileNative(int argCount, Value* args)
{
	int fd;
	long len;
	char *buf;
	Dir *d;
	long bytesRead;
	Value result;

	if (argCount != 1 || !IS_STRING(args[0]))
		return NIL_VAL;

	char* path = AS_CSTRING(args[0]);
	
	/* open() returns a file descriptor integer */
	fd = open(path, OREAD);
	if(fd < 0)
		return NIL_VAL;

	/* dirfstat is the Plan 9 way to get file metadata like length */
	d = dirfstat(fd);
	if(d == nil) {
		close(fd);
		return NIL_VAL;
	}
	len = d->length;
	free(d);

	buf = malloc(len + 1);
	if(buf == nil) {
		close(fd);
		return NIL_VAL;
	}

	bytesRead = read(fd, buf, len);
	if(bytesRead < 0) {
		free(buf);
		close(fd);
		return NIL_VAL;
	}
	
	buf[bytesRead] = '\0';
	close(fd);

	/* Wrap the raw C string into a Lux Value */
	result = OBJ_VAL(copyString(buf, (int)bytesRead));
	free(buf);
	return result;
}

/* renderTemplate(path, ctx) -> string; ctx must be an instance */
static Value
renderTemplateNative(int argCount, Value* args)
{
	char* path;
	ObjInstance* ctx;
	char* html;
	Value result;

	if (argCount != 2) {
		nativeError("renderTemplate() expects 2 arguments (path, ctx), got %d.", argCount);
		return NIL_VAL;
	}
	if (!IS_STRING(args[0])) {
		nativeError("renderTemplate() path must be a string.");
		return NIL_VAL;
	}
	if (!IS_INSTANCE(args[1])) {
		nativeError("renderTemplate() ctx must be an instance.");
		return NIL_VAL;
	}

	path = AS_CSTRING(args[0]);
	ctx = AS_INSTANCE(args[1]);
	html = tplRenderFull(path, ctx);
	if (html == nil) {
		nativeError("renderTemplate() failed.");
		return NIL_VAL;
	}
	result = OBJ_VAL(copyString(html, (int)strlen(html)));
	free(html);
	return result;
}

/* markdownToHtml(md) -> string */
static Value
markdownToHtmlNative(int argCount, Value* args)
{
	char* html;
	Value result;

	if (argCount != 1) {
		nativeError("markdownToHtml() expects 1 argument, got %d.", argCount);
		return NIL_VAL;
	}
	if (!IS_STRING(args[0])) {
		nativeError("markdownToHtml() expects a string.");
		return NIL_VAL;
	}
	html = mdToHtml(AS_CSTRING(args[0]), AS_STRING(args[0])->length);
	if (html == nil) {
		nativeError("markdownToHtml() failed.");
		return NIL_VAL;
	}
	result = OBJ_VAL(copyString(html, (int)strlen(html)));
	free(html);
	return result;
}

/* renderMarkdown(path) -> string */
static Value
renderMarkdownNative(int argCount, Value* args)
{
	char* html;
	Value result;

	if (argCount != 1) {
		nativeError("renderMarkdown() expects 1 argument, got %d.", argCount);
		return NIL_VAL;
	}
	if (!IS_STRING(args[0])) {
		nativeError("renderMarkdown() path must be a string.");
		return NIL_VAL;
	}
	html = mdRenderFile(AS_CSTRING(args[0]));
	if (html == nil) {
		nativeError("renderMarkdown() failed.");
		return NIL_VAL;
	}
	result = OBJ_VAL(copyString(html, (int)strlen(html)));
	free(html);
	return result;
}

/* Native function to write a file: writeFile(path, content) -> bool */
static Value 
writeFileNative(int argCount, Value* args)
{
	int fd;
	char* path;
	char* content;
	int contentLen;
	long bytesWritten;

	if (argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1]))
		return BOOL_VAL(false);

	path = AS_CSTRING(args[0]);
	content = AS_CSTRING(args[1]);
	contentLen = AS_STRING(args[1])->length;

	/* create() with OWRITE creates or truncates the file */
	fd = create(path, OWRITE, 0666);
	if(fd < 0)
		return BOOL_VAL(false);

	bytesWritten = write(fd, content, contentLen);
	close(fd);

	if(bytesWritten != contentLen)
		return BOOL_VAL(false);

	return BOOL_VAL(true);
}

/* Native function to append to a file: appendFile(path, content) -> bool */
static Value 
appendFileNative(int argCount, Value* args)
{
	int fd;
	char* path;
	char* content;
	int contentLen;
	long bytesWritten;

	if (argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1]))
		return BOOL_VAL(false);

	path = AS_CSTRING(args[0]);
	content = AS_CSTRING(args[1]);
	contentLen = AS_STRING(args[1])->length;

	/* open with OWRITE, file must exist */
	fd = open(path, OWRITE);
	if(fd < 0)
		return BOOL_VAL(false);

	/* seek to end of file */
	seek(fd, 0, 2);

	bytesWritten = write(fd, content, contentLen);
	close(fd);

	if(bytesWritten != contentLen)
		return BOOL_VAL(false);

	return BOOL_VAL(true);
}

/* Native function to delete a file: deleteFile(path) -> bool */
static Value 
deleteFileNative(int argCount, Value* args)
{
	if (argCount != 1 || !IS_STRING(args[0]))
		return BOOL_VAL(false);

	char* path = AS_CSTRING(args[0]);
	
	/* remove() works for files and empty directories */
	if(remove(path) < 0)
		return BOOL_VAL(false);

	return BOOL_VAL(true);
}

/* Native function to check if file exists: fileExists(path) -> bool */
static Value 
fileExistsNative(int argCount, Value* args)
{
	if (argCount != 1 || !IS_STRING(args[0]))
		return BOOL_VAL(false);

	char* path = AS_CSTRING(args[0]);
	
	/* access() with AEXIST checks if file exists */
	if(access(path, AEXIST) == 0)
		return BOOL_VAL(true);
	
	return BOOL_VAL(false);
}

/* Native function to create a directory: createDir(path) -> bool */
static Value 
createDirNative(int argCount, Value* args)
{
	int fd;

	if (argCount != 1 || !IS_STRING(args[0]))
		return BOOL_VAL(false);

	char* path = AS_CSTRING(args[0]);
	
	/* create with DMDIR flag creates a directory */
	fd = create(path, OREAD, DMDIR | 0775);
	if(fd < 0)
		return BOOL_VAL(false);

	close(fd);
	return BOOL_VAL(true);
}

/* Native function to list directory contents: listDir(path) -> string */
static Value 
listDirNative(int argCount, Value* args)
{
	int fd, n, i;
	Dir *dirs;
	char* path;
	char* result;
	int resultLen, resultCap;
	Value retval;

	if (argCount != 1 || !IS_STRING(args[0]))
		return NIL_VAL;

	path = AS_CSTRING(args[0]);
	
	/* open directory for reading */
	fd = open(path, OREAD);
	if(fd < 0)
		return NIL_VAL;

	/* allocate buffer for result string */
	resultCap = 1024;
	result = malloc(resultCap);
	if(result == nil) {
		close(fd);
		return NIL_VAL;
	}
	resultLen = 0;
	result[0] = '\0';

	/* read directory entries */
	while((n = dirread(fd, &dirs)) > 0) {
		for(i = 0; i < n; i++) {
			int nameLen = strlen(dirs[i].name);
			
			/* ensure buffer is large enough */
			while(resultLen + nameLen + 2 > resultCap) {
				resultCap *= 2;
				char* newResult = realloc(result, resultCap);
				if(newResult == nil) {
					free(dirs);
					free(result);
					close(fd);
					return NIL_VAL;
				}
				result = newResult;
			}
			
			/* append name and newline */
			strcpy(result + resultLen, dirs[i].name);
			resultLen += nameLen;
			result[resultLen++] = '\n';
			result[resultLen] = '\0';
		}
		free(dirs);
	}

	close(fd);

	/* remove trailing newline if present */
	if(resultLen > 0 && result[resultLen-1] == '\n') {
		result[resultLen-1] = '\0';
		resultLen--;
	}

	retval = OBJ_VAL(copyString(result, resultLen));
	free(result);
	return retval;
}

/* run(cmd) -> string (stdout) or nil.  Uses fork/exec with rc -c. */
static Value
runNative(int argCount, Value* args)
{
	int pipeFd[2];
	int pid;
	int n, total;
	long cap;
	char *cmd;
	char *buf;
	char *newBuf;
	Waitmsg *w;
	Value result;

	if (argCount != 1 || !IS_STRING(args[0]))
		return NIL_VAL;

	cmd = AS_CSTRING(args[0]);

	if (pipe(pipeFd) < 0)
		return NIL_VAL;

	pid = fork();
	if (pid < 0) {
		close(pipeFd[0]);
		close(pipeFd[1]);
		return NIL_VAL;
	}

	if (pid == 0) {
		char *args[4];

		/* Child: redirect stdout to pipe, discard stderr */
		close(pipeFd[0]);
		dup(pipeFd[1], 1);
		close(pipeFd[1]);
		dup(open("/dev/null", OWRITE), 2);

		args[0] = "rc";
		args[1] = "-c";
		args[2] = cmd;
		args[3] = nil;
		exec("/bin/rc", args);
		exits("exec failed");
	}

	/* Parent: read stdout from pipe */
	close(pipeFd[1]);
	cap = 4096;
	total = 0;
	buf = malloc(cap);
	if (buf == nil) {
		close(pipeFd[0]);
		return NIL_VAL;
	}
	for (;;) {
		if ((long)total >= cap - 1) {
			cap *= 2;
			newBuf = realloc(buf, cap);
			if (newBuf == nil) {
				free(buf);
				close(pipeFd[0]);
				return NIL_VAL;
			}
			buf = newBuf;
		}
		n = read(pipeFd[0], buf + total, cap - 1 - total);
		if (n <= 0)
			break;
		total += n;
	}
	buf[total] = '\0';
	close(pipeFd[0]);

	/* Wait for child */
	for (;;) {
		w = wait();
		if (w == nil)
			break;
		if (w->pid == pid) {
			free(w);
			break;
		}
		free(w);
	}

	result = OBJ_VAL(copyString(buf, total));
	free(buf);
	return result;
}

/* netLookup(host) -> first resolved IP as a string, or nil.
 * Queries the connection server (/net/cs), which performs DNS. */
static Value
netLookupNative(int argCount, Value* args)
{
	int fd, n;
	char query[300];
	char buf[256];
	char *addr, *bang;

	if (argCount != 1 || !IS_STRING(args[0]))
		return NIL_VAL;

	fd = open("/net/cs", ORDWR);
	if (fd < 0)
		return NIL_VAL;

	snprint(query, sizeof(query), "tcp!%s!1", AS_CSTRING(args[0]));
	if (write(fd, query, strlen(query)) < 0) {
		close(fd);
		return NIL_VAL;
	}

	seek(fd, 0, 0);
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return NIL_VAL;
	buf[n] = '\0';

	/* Reply line: "/net/tcp/clone 1.2.3.4!1" — take the address after the space. */
	addr = strchr(buf, ' ');
	if (addr == nil)
		return NIL_VAL;
	addr++;
	bang = strchr(addr, '!');
	if (bang != nil)
		*bang = '\0';

	return OBJ_VAL(copyString(addr, strlen(addr)));
}

/* netPing(host, port) -> round-trip milliseconds, or -1 on failure.
 * TCP-connect probe via dial(); timing includes name resolution. */
static Value
netPingNative(int argCount, Value* args)
{
	int fd, port;
	char dialAddr[300];
	vlong start, end;

	if (argCount != 2 || !IS_STRING(args[0]) || !IS_NUMBER(args[1]))
		return NUMBER_VAL(-1);

	port = (int)AS_NUMBER(args[1]);
	if (port <= 0 || port > 65535)
		return NUMBER_VAL(-1);

	snprint(dialAddr, sizeof(dialAddr), "tcp!%s!%d", AS_CSTRING(args[0]), port);

	start = nsec();
	fd = dial(dialAddr, nil, nil, nil);
	if (fd < 0)
		return NUMBER_VAL(-1);
	end = nsec();
	close(fd);

	return NUMBER_VAL((double)(end - start) / 1000000.0);
}

static bool
isAsciiWhitespace(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static bool
stringMatchAt(const char* text, int textLen, int index, const char* pattern, int patternLen)
{
	if (index < 0 || index + patternLen > textLen)
		return false;

	for (int i = 0; i < patternLen; i++) {
		if (text[index + i] != pattern[i])
			return false;
	}

	return true;
}

/* len(string|array) -> number */
static Value
lenNative(int argCount, Value* args)
{
	if (argCount != 1)
		return NIL_VAL;
	if (IS_STRING(args[0]))
		return NUMBER_VAL(AS_STRING(args[0])->length);
	if (IS_ARRAY(args[0]))
		return NUMBER_VAL((double)AS_ARRAY(args[0])->count);
	if (IS_FLOATARRAY(args[0]))
		return NUMBER_VAL((double)AS_FLOATARRAY(args[0])->length);
	return NIL_VAL;
}

/* args() -> array of strings (script arguments after path) */
static Value
argsNative(int argCount, Value* args)
{
	(void)args;
	if (argCount != 0)
		return NIL_VAL;
	if (vm.scriptArgs == nil)
		return OBJ_VAL(newArray());
	return OBJ_VAL(vm.scriptArgs);
}

void
setScriptArgs(int argc, char** argv)
{
	ObjArray* arr;
	int i;

	arr = newArray();
	push(OBJ_VAL(arr));
	for (i = 0; i < argc; i++)
		writeArray(arr, OBJ_VAL(copyString(argv[i], (int)strlen(argv[i]))));
	vm.scriptArgs = arr;
	pop();
}

/* strFind(haystack, needle, [start]) -> number index or -1 */
static Value
strFindNative(int argCount, Value* args)
{
	if (argCount < 2 || argCount > 3)
		return NIL_VAL;

	if (!IS_STRING(args[0]) || !IS_STRING(args[1]))
		return NIL_VAL;

	ObjString* haystack = AS_STRING(args[0]);
	ObjString* needle = AS_STRING(args[1]);
	int start = 0;

	if (argCount == 3) {
		if (!IS_NUMBER(args[2]))
			return NIL_VAL;
		start = (int)AS_NUMBER(args[2]);
	}

	if (start < 0)
		start = 0;
	if (start > haystack->length)
		return NUMBER_VAL(-1);

	if (needle->length == 0)
		return NUMBER_VAL(start);

	int lastStart = haystack->length - needle->length;
	for (int i = start; i <= lastStart; i++) {
		if (stringMatchAt(haystack->chars, haystack->length, i,
				needle->chars, needle->length)) {
			return NUMBER_VAL(i);
		}
	}

	return NUMBER_VAL(-1);
}

/* strStartsWithAt(text, pattern, index) -> bool */
static Value
strStartsWithAtNative(int argCount, Value* args)
{
	if (argCount != 3)
		return NIL_VAL;

	if (!IS_STRING(args[0]) || !IS_STRING(args[1]) || !IS_NUMBER(args[2]))
		return NIL_VAL;

	ObjString* text = AS_STRING(args[0]);
	ObjString* pattern = AS_STRING(args[1]);
	int index = (int)AS_NUMBER(args[2]);

	return BOOL_VAL(stringMatchAt(text->chars, text->length, index,
			pattern->chars, pattern->length));
}

/* strSlice(text, start, end) -> string */
static Value
strSliceNative(int argCount, Value* args)
{
	if (argCount != 3)
		return NIL_VAL;

	if (!IS_STRING(args[0]) || !IS_NUMBER(args[1]) || !IS_NUMBER(args[2]))
		return NIL_VAL;

	ObjString* text = AS_STRING(args[0]);
	int start = (int)AS_NUMBER(args[1]);
	int end = (int)AS_NUMBER(args[2]);

	if (start < 0) start = 0;
	if (end < 0) end = 0;
	if (start > text->length) start = text->length;
	if (end > text->length) end = text->length;
	if (end < start) end = start;

	return OBJ_VAL(copyString(text->chars + start, end - start));
}

/* strTrim(text) -> string */
static Value
strTrimNative(int argCount, Value* args)
{
	if (argCount != 1 || !IS_STRING(args[0]))
		return NIL_VAL;

	ObjString* text = AS_STRING(args[0]);
	int start = 0;
	int end = text->length;

	while (start < end && isAsciiWhitespace(text->chars[start]))
		start++;

	while (end > start && isAsciiWhitespace(text->chars[end - 1]))
		end--;

	return OBJ_VAL(copyString(text->chars + start, end - start));
}

static Value
getFieldNative(int argCount, Value* args)
{
	Value value;

	if (argCount != 2 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1]))
		return NIL_VAL;
	if (!tableGet(&AS_INSTANCE(args[0])->fields, AS_STRING(args[1]), &value))
		return NIL_VAL;
	return value;
}

static void
csvAppendChar(char **buf, int *len, int *cap, char c)
{
	char *nbuf;
	int ncap;

	if (*len + 1 >= *cap) {
		ncap = (*cap < 8) ? 64 : (*cap) * 2;
		nbuf = realloc(*buf, ncap);
		if (nbuf == nil)
			return;
		*buf = nbuf;
		*cap = ncap;
	}
	(*buf)[(*len)++] = c;
}

static void
csvPushTrimmed(ObjArray *row, char *field, int fieldLen)
{
	int start, end;

	start = 0;
	end = fieldLen;
	while (start < end && isAsciiWhitespace(field[start]))
		start++;
	while (end > start && isAsciiWhitespace(field[end - 1]))
		end--;
	writeArray(row, OBJ_VAL(copyString(field + start, end - start)));
}

static Value
parseCSVNative(int argCount, Value* args)
{
	ObjString *text;
	char sep;
	ObjArray *rows, *row;
	char *field, *s;
	int fieldLen, fieldCap, inQuotes, L, i;
	char ch;

	if (argCount < 1 || argCount > 2 || !IS_STRING(args[0]))
		return NIL_VAL;

	text = AS_STRING(args[0]);
	sep = ',';
	if (argCount == 2 && !IS_NIL(args[1])) {
		if (!IS_STRING(args[1]) || AS_STRING(args[1])->length < 1)
			return NIL_VAL;
		sep = AS_STRING(args[1])->chars[0];
	}

	rows = newArray();
	push(OBJ_VAL(rows));
	row = newArray();
	push(OBJ_VAL(row));

	field = malloc(64);
	if (field == nil) {
		pop();
		pop();
		return NIL_VAL;
	}
	fieldLen = 0;
	fieldCap = 64;
	inQuotes = 0;
	s = text->chars;
	L = text->length;
	i = 0;

	while (i < L) {
		ch = s[i];
		if (ch == '\r') {
			i++;
			continue;
		}
		if (inQuotes) {
			if (ch == '"') {
				if (i + 1 < L && s[i + 1] == '"') {
					csvAppendChar(&field, &fieldLen, &fieldCap, '"');
					i += 2;
					continue;
				}
				inQuotes = 0;
				i++;
				continue;
			}
			csvAppendChar(&field, &fieldLen, &fieldCap, ch);
			i++;
			continue;
		}
		if (ch == '"') {
			inQuotes = 1;
			i++;
			continue;
		}
		if (ch == sep) {
			csvPushTrimmed(row, field, fieldLen);
			fieldLen = 0;
			i++;
			continue;
		}
		if (ch == '\n') {
			csvPushTrimmed(row, field, fieldLen);
			fieldLen = 0;
			writeArray(rows, OBJ_VAL(row));
			pop();
			row = newArray();
			push(OBJ_VAL(row));
			i++;
			continue;
		}
		csvAppendChar(&field, &fieldLen, &fieldCap, ch);
		i++;
	}

	if (fieldLen > 0 || row->count > 0) {
		csvPushTrimmed(row, field, fieldLen);
		writeArray(rows, OBJ_VAL(row));
	}

	free(field);
	pop();
	return pop();
}

/* strSplit(text, sep) -> array of strings */
static Value
strSplitNative(int argCount, Value* args)
{
	if (argCount != 2)
		return NIL_VAL;

	if (!IS_STRING(args[0]) || !IS_STRING(args[1]))
		return NIL_VAL;

	ObjString* text = AS_STRING(args[0]);
	ObjString* sep = AS_STRING(args[1]);

	ObjArray* result = newArray();
	push(OBJ_VAL(result));

	if (sep->length == 0) {
		for (int i = 0; i < text->length; i++) {
			Value token = OBJ_VAL(copyString(text->chars + i, 1));
			push(token);
			writeArray(result, token);
			pop();
		}
		pop();
		return OBJ_VAL(result);
	}

	int tokenStart = 0;
	for (int i = 0; i <= text->length - sep->length; ) {
		if (stringMatchAt(text->chars, text->length, i, sep->chars, sep->length)) {
			Value token = OBJ_VAL(copyString(text->chars + tokenStart, i - tokenStart));
			push(token);
			writeArray(result, token);
			pop();

			i += sep->length;
			tokenStart = i;
		} else {
			i++;
		}
	}

	Value tail = OBJ_VAL(copyString(text->chars + tokenStart, text->length - tokenStart));
	push(tail);
	writeArray(result, tail);
	pop();

	pop();
	return OBJ_VAL(result);
}

static int
compareValuesForSort(Value a, Value b, bool* ok)
{
	if (IS_NUMBER(a) && IS_NUMBER(b)) {
		double da = AS_NUMBER(a);
		double db = AS_NUMBER(b);
		*ok = true;
		if (da < db) return -1;
		if (da > db) return 1;
		return 0;
	}

	if (IS_STRING(a) && IS_STRING(b)) {
		int cmp = strcmp(AS_CSTRING(a), AS_CSTRING(b));
		*ok = true;
		if (cmp < 0) return -1;
		if (cmp > 0) return 1;
		return 0;
	}

	*ok = false;
	return 0;
}

/* arrayIndexOf(array, value) -> number index or -1 */
static Value
arrayIndexOfNative(int argCount, Value* args)
{
	if (argCount != 2 || !IS_ARRAY(args[0]))
		return NIL_VAL;

	ObjArray* array = AS_ARRAY(args[0]);
	Value needle = args[1];

	for (int i = 0; i < array->count; i++) {
		if (valuesEqual(array->elements[i], needle))
			return NUMBER_VAL(i);
	}

	return NUMBER_VAL(-1);
}

/* arrayContains(array, value) -> bool */
static Value
arrayContainsNative(int argCount, Value* args)
{
	if (argCount != 2 || !IS_ARRAY(args[0]))
		return NIL_VAL;

	ObjArray* array = AS_ARRAY(args[0]);
	Value needle = args[1];

	for (int i = 0; i < array->count; i++) {
		if (valuesEqual(array->elements[i], needle))
			return BOOL_VAL(true);
	}

	return BOOL_VAL(false);
}

/* arraySort(array) -> array (in-place). Supports all-number or all-string arrays. */
static Value
arraySortNative(int argCount, Value* args)
{
	if (argCount != 1 || !IS_ARRAY(args[0]))
		return NIL_VAL;

	ObjArray* array = AS_ARRAY(args[0]);
	if (array->count < 2)
		return args[0];

	for (int i = 0; i < array->count - 1; i++) {
		for (int j = 0; j < array->count - 1 - i; j++) {
			bool ok;
			int cmp = compareValuesForSort(array->elements[j], array->elements[j + 1], &ok);
			if (!ok)
				return NIL_VAL;

			if (cmp > 0) {
				Value tmp = array->elements[j];
				array->elements[j] = array->elements[j + 1];
				array->elements[j + 1] = tmp;
			}
		}
	}

	return args[0];
}

/* arrayBinarySearch(array, value) -> number index or -1.
 * Array must already be sorted and value type must match element type.
 */
static Value
arrayBinarySearchNative(int argCount, Value* args)
{
	if (argCount != 2 || !IS_ARRAY(args[0]))
		return NIL_VAL;

	ObjArray* array = AS_ARRAY(args[0]);
	Value needle = args[1];

	int left = 0;
	int right = array->count - 1;

	while (left <= right) {
		int mid = left + (right - left) / 2;
		bool ok;
		int cmp = compareValuesForSort(array->elements[mid], needle, &ok);
		if (!ok)
			return NUMBER_VAL(-1);

		if (cmp == 0)
			return NUMBER_VAL(mid);
		if (cmp < 0)
			left = mid + 1;
		else
			right = mid - 1;
	}

	return NUMBER_VAL(-1);
}

/* JSON parsing helpers */
typedef struct {
	char* start;
	char* current;
} JsonParser;

static void skipWhitespace(JsonParser* parser) {
	while (*parser->current == ' ' || *parser->current == '\t' || 
	       *parser->current == '\n' || *parser->current == '\r') {
		parser->current++;
	}
}

static bool matchChar(JsonParser* parser, char c) {
	if (*parser->current == c) {
		parser->current++;
		return true;
	}
	return false;
}

static Value parseJsonValue(JsonParser* parser);

static Value parseJsonString(JsonParser* parser) {
	parser->current++; /* skip opening " */
	char* start = parser->current;
	int len = 0;
	
	while (*parser->current != '"' && *parser->current != '\0') {
		if (*parser->current == '\\') {
			parser->current++; /* skip escape char for now */
			if (*parser->current != '\0') parser->current++;
			len += 2; /* simplified: count both chars */
		} else {
			parser->current++;
			len++;
		}
	}
	
	if (*parser->current != '"') return NIL_VAL; /* error */
	
	/* simplified: just copy the string without unescaping */
	char* str = malloc(len + 1);
	if (str == nil) return NIL_VAL;
	
	char* dst = str;
	char* src = start;
	while (src < parser->current) {
		if (*src == '\\' && src + 1 < parser->current) {
			src++; /* skip backslash */
			switch (*src) {
				case 'n': *dst++ = '\n'; break;
				case 't': *dst++ = '\t'; break;
				case 'r': *dst++ = '\r'; break;
				case '"': *dst++ = '"'; break;
				case '\\': *dst++ = '\\'; break;
				default: *dst++ = *src; break;
			}
			src++;
		} else {
			*dst++ = *src++;
		}
	}
	*dst = '\0';
	
	parser->current++; /* skip closing " */
	Value result = OBJ_VAL(copyString(str, (int)(dst - str)));
	free(str);
	return result;
}

static Value parseJsonNumber(JsonParser* parser) {
	char* start = parser->current;
	
	if (*parser->current == '-') parser->current++;
	
	while (*parser->current >= '0' && *parser->current <= '9') {
		parser->current++;
	}
	
	if (*parser->current == '.') {
		parser->current++;
		while (*parser->current >= '0' && *parser->current <= '9') {
			parser->current++;
		}
	}
	
	char* end = parser->current;
	char saved = *end;
	*end = '\0';
	double num = strtod(start, nil);
	*end = saved;
	
	return NUMBER_VAL(num);
}

static Value parseJsonArray(JsonParser* parser) {
	parser->current++; /* skip [ */
	skipWhitespace(parser);

	ObjArray* array = newArray();
	push(OBJ_VAL(array)); /* protect from GC */

	if (*parser->current != ']') {
		for (;;) {
			skipWhitespace(parser);
			Value elem = parseJsonValue(parser);
			push(elem); /* protect from GC during grow */
			writeArray(array, elem);
			pop();

			skipWhitespace(parser);
			if (!matchChar(parser, ',')) break;
		}
	}

	skipWhitespace(parser);
	if (*parser->current == ']') parser->current++;

	pop(); /* array */
	return OBJ_VAL(array);
}

static Value parseJsonObject(JsonParser* parser) {
	parser->current++; /* skip { */
	skipWhitespace(parser);
	
	/* Create an instance to hold object properties */
	ObjClass* objClass = newClass(copyString("Object", 6));
	push(OBJ_VAL(objClass)); /* protect from GC */
	ObjInstance* instance = newInstance(objClass);
	push(OBJ_VAL(instance)); /* protect from GC */
	
	if (*parser->current != '}') {
		for (;;) {
			skipWhitespace(parser);
			
			/* Parse key */
			if (*parser->current != '"') {
				pop(); /* instance */
				pop(); /* objClass */
				return NIL_VAL; /* error */
			}
			
			Value keyVal = parseJsonString(parser);
			if (!IS_STRING(keyVal)) {
				pop();
				pop();
				return NIL_VAL;
			}
			ObjString* key = AS_STRING(keyVal);
			push(OBJ_VAL(key)); /* protect from GC */
			
			skipWhitespace(parser);
			if (!matchChar(parser, ':')) {
				pop(); /* key */
				pop(); /* instance */
				pop(); /* objClass */
				return NIL_VAL; /* error */
			}
			
			skipWhitespace(parser);
			Value value = parseJsonValue(parser);
			
			/* Protect value from GC during tableSet */
			push(value);
			tableSet(&instance->fields, key, value);
			pop();
			pop(); /* key */
			
			skipWhitespace(parser);
			if (!matchChar(parser, ',')) break;
		}
	}
	
	skipWhitespace(parser);
	if (*parser->current == '}') parser->current++;
	
	pop(); /* instance */
	pop(); /* objClass */
	return OBJ_VAL(instance);
}

static Value parseJsonValue(JsonParser* parser) {
	skipWhitespace(parser);
	
	if (*parser->current == '"') {
		return parseJsonString(parser);
	} else if (*parser->current == '{') {
		return parseJsonObject(parser);
	} else if (*parser->current == '[') {
		return parseJsonArray(parser);
	} else if (*parser->current == 't') {
		if (strncmp(parser->current, "true", 4) == 0) {
			parser->current += 4;
			return BOOL_VAL(true);
		}
	} else if (*parser->current == 'f') {
		if (strncmp(parser->current, "false", 5) == 0) {
			parser->current += 5;
			return BOOL_VAL(false);
		}
	} else if (*parser->current == 'n') {
		if (strncmp(parser->current, "null", 4) == 0) {
			parser->current += 4;
			return NIL_VAL;
		}
	} else if (*parser->current == '-' || (*parser->current >= '0' && *parser->current <= '9')) {
		return parseJsonNumber(parser);
	}
	
	return NIL_VAL;
}

/* Native function to parse JSON: parseJSON(jsonString) -> value */
static Value 
parseJSONNative(int argCount, Value* args)
{
	if (argCount != 1 || !IS_STRING(args[0]))
		return NIL_VAL;
	
	char* jsonStr = AS_CSTRING(args[0]);
	
	JsonParser parser;
	parser.start = jsonStr;
	parser.current = jsonStr;
	
	return parseJsonValue(&parser);
}

/* JSON serialization helper */
static void appendToBuffer(char** buffer, int* len, int* cap, char* str) {
	int addLen = strlen(str);
	while (*len + addLen + 1 > *cap) {
		*cap *= 2;
		char* newBuf = realloc(*buffer, *cap);
		if (newBuf == nil) return;
		*buffer = newBuf;
	}
	strcpy(*buffer + *len, str);
	*len += addLen;
}

static void appendChar(char** buffer, int* len, int* cap, char c) {
	if (*len + 2> *cap) {
		*cap *= 2;
		char* newBuf = realloc(*buffer, *cap);
		if (newBuf == nil) return;
		*buffer = newBuf;
	}
	(*buffer)[(*len)++] = c;
	(*buffer)[*len] = '\0';
}

/* Append a string escaped for safe JSON string value embedding. */
static void
appendJsonEscaped(char** buffer, int* len, int* cap, char* str)
{
	while (*str) {
		uchar c;
		char esc[8];

		c = (uchar)*str++;
		switch (c) {
		case '"': appendToBuffer(buffer, len, cap, "\\\""); break;
		case '\\': appendToBuffer(buffer, len, cap, "\\\\"); break;
		case '\n': appendToBuffer(buffer, len, cap, "\\n"); break;
		case '\t': appendToBuffer(buffer, len, cap, "\\t"); break;
		case '\r': appendToBuffer(buffer, len, cap, "\\r"); break;
		default:
			if (c < 0x20) {
				snprint(esc, sizeof(esc), "\\u%04x", c);
				appendToBuffer(buffer, len, cap, esc);
			} else {
				appendChar(buffer, len, cap, (char)c);
			}
			break;
		}
	}
}

static void serializeJsonValue(Value value, char** buffer, int* len, int* cap);

static void serializeJsonObject(ObjInstance* instance, char** buffer, int* len, int* cap) {
	appendChar(buffer, len, cap, '{');
	
	bool first = true;
	for (int i = 0; i < instance->fields.capacity; i++) {
		if (instance->fields.entries[i].key != nil) {
			if (!first) appendChar(buffer, len, cap, ',');
			first = false;
			
			/* Add key */
			appendChar(buffer, len, cap, '"');
			appendToBuffer(buffer, len, cap, instance->fields.entries[i].key->chars);
			appendChar(buffer, len, cap, '"');
			appendChar(buffer, len, cap, ':');
			
			/* Add value */
			serializeJsonValue(instance->fields.entries[i].value, buffer, len, cap);
		}
	}
	
	appendChar(buffer, len, cap, '}');
}

static void serializeJsonValue(Value value, char** buffer, int* len, int* cap) {
	if (IS_BOOL(value)) {
		appendToBuffer(buffer, len, cap, AS_BOOL(value) ? "true" : "false");
	} else if (IS_NIL(value)) {
		appendToBuffer(buffer, len, cap, "null");
	} else if (IS_NUMBER(value)) {
		char numBuf[64];
		sprint(numBuf, "%.15g", AS_NUMBER(value));
		appendToBuffer(buffer, len, cap, numBuf);
	} else if (IS_STRING(value)) {
		appendChar(buffer, len, cap, '"');
		char* str = AS_CSTRING(value);
		while (*str) {
			if (*str == '"') appendToBuffer(buffer, len, cap, "\\\"");
			else if (*str == '\\') appendToBuffer(buffer, len, cap, "\\\\");
			else if (*str == '\n') appendToBuffer(buffer, len, cap, "\\n");
			else if (*str == '\t') appendToBuffer(buffer, len, cap, "\\t");
			else if (*str == '\r') appendToBuffer(buffer, len, cap, "\\r");
			else appendChar(buffer, len, cap, *str);
			str++;
		}
		appendChar(buffer, len, cap, '"');
	} else if (IS_ARRAY(value)) {
		ObjArray* arr = AS_ARRAY(value);
		appendChar(buffer, len, cap, '[');
		for (int i = 0; i < arr->count; i++) {
			if (i > 0) appendChar(buffer, len, cap, ',');
			serializeJsonValue(arr->elements[i], buffer, len, cap);
		}
		appendChar(buffer, len, cap, ']');
	} else if (IS_INSTANCE(value)) {
		serializeJsonObject(AS_INSTANCE(value), buffer, len, cap);
	}
}

/* Native function to convert to JSON: toJSON(value) -> string */
static Value 
toJSONNative(int argCount, Value* args)
{
	if (argCount != 1)
		return NIL_VAL;
	
	int cap = 256;
	int len = 0;
	char* buffer = malloc(cap);
	if (buffer == nil) return NIL_VAL;
	buffer[0] = '\0';
	
	serializeJsonValue(args[0], &buffer, &len, &cap);
	
	Value result = OBJ_VAL(copyString(buffer, len));
	free(buffer);
	return result;
}

/* parseXml(xmlString, tagName) -> object with numeric keys (like JSON arrays) */
static Value
parseXmlNative(int argCount, Value* args)
{
	if (argCount != 2) {
		nativeError("parseXml() expects 2 arguments (xmlString, tagName), got %d.", argCount);
		return NIL_VAL;
	}

	if (!IS_STRING(args[0])) {
		nativeError("parseXml() first argument must be a string (xmlString).");
		return NIL_VAL;
	}

	if (!IS_STRING(args[1])) {
		nativeError("parseXml() second argument must be a string (tagName).");
		return NIL_VAL;
	}
	
	char* xml = AS_CSTRING(args[0]);
	char* tagName = AS_CSTRING(args[1]);
	int tagLen = strlen(tagName);
	if (tagLen == 0) {
		nativeError("parseXml() tagName cannot be empty.");
		return NIL_VAL;
	}
	
	/* Build open and close tags */
	char openTag[256];
	char closeTag[256];
	if (tagLen > (int)sizeof(closeTag) - 4) {
		nativeError("parseXml() tagName too long (max %d bytes).", (int)sizeof(closeTag) - 4);
		return NIL_VAL;
	}
	snprint(openTag, sizeof(openTag), "<%s>", tagName);
	snprint(closeTag, sizeof(closeTag), "</%s>", tagName);
	int openLen = strlen(openTag);
	int closeLen = strlen(closeTag);
	
	/* Create result array */
	ObjArray* result = newArray();
	push(OBJ_VAL(result));
	
	char* search = xml;
	
	/* Find all occurrences of the tag */
	while (1) {
		/* Find opening tag */
		char* openPos = strstr(search, openTag);
		if (openPos == nil)
			break;
		
		/* Find closing tag after opening */
		char* closePos = strstr(openPos + openLen, closeTag);
		if (closePos == nil)
			break;
		
		/* Extract content between tags */
		int contentLen = closePos - (openPos + openLen);
		char* content = malloc(contentLen + 1);
		if (content == nil)
			break;
		
		memcpy(content, openPos + openLen, contentLen);
		content[contentLen] = '\0';
		
		/* Add to result array */
		Value value = OBJ_VAL(copyString(content, contentLen));
		free(content);
		
		writeArray(result, value);
		
		/* Move search position past this closing tag */
		search = closePos + closeLen;
	}
	
	pop();
	return OBJ_VAL(result);
}

/* HTTP support for Plan 9 using dial() and manual HTTP protocol */

/* Parse URL and extract host, port, path, and https flag */
typedef struct {
	char host[256];
	char port[16];
	char path[1024];
	int ishttps;
} UrlParts;

static int
parseUrl(char* url, UrlParts* parts)
{
	char* p = url;
	
	/* Check for https:// or http:// */
	if (strncmp(p, "https://", 8) == 0) {
		parts->ishttps = 1;
		p += 8;
		strcpy(parts->port, "443");
	} else if (strncmp(p, "http://", 7) == 0) {
		parts->ishttps = 0;
		p += 7;
		strcpy(parts->port, "80");
	} else {
		return -1;
	}
	
	/* Extract host (up to : or /) */
	char* hostEnd = p;
	while (*hostEnd && *hostEnd != ':' && *hostEnd != '/')
		hostEnd++;
	
	int hostLen = hostEnd - p;
	if (hostLen >= 256) return -1;
	strncpy(parts->host, p, hostLen);
	parts->host[hostLen] = '\0';
	
	/* Check for explicit port */
	if (*hostEnd == ':') {
		p = hostEnd + 1;
		char* portEnd = p;
		while (*portEnd && *portEnd != '/')
			portEnd++;
		int portLen = portEnd - p;
		if (portLen >= 16) return -1;
		strncpy(parts->port, p, portLen);
		parts->port[portLen] = '\0';
		hostEnd = portEnd;
	}
	
	/* Extract path */
	if (*hostEnd == '/') {
		strncpy(parts->path, hostEnd, 1023);
		parts->path[1023] = '\0';
	} else {
		strcpy(parts->path, "/");
	}
	
	return 0;
}

/* Plan 9 write() may return a short count; loop until all bytes are sent. */
static int
writeAll(int fd, char* buf, int n)
{
	int sent;

	sent = 0;
	while (sent < n) {
		int w = write(fd, buf + sent, n - sent);
		if (w <= 0)
			return -1;
		sent += w;
	}
	return sent;
}

/* Read HTTP response and extract body */
static char*
readHttpResponse(int fd, int* outLen)
{
	int cap = 4096;
	int len = 0;
	char* buffer = malloc(cap);
	if (buffer == nil) return nil;
	
	/* Read response */
	while (1) {
		if (len >= cap - 1) {
			cap *= 2;
			char* newbuf = realloc(buffer, cap);
			if (newbuf == nil) {
				free(buffer);
				return nil;
			}
			buffer = newbuf;
		}
		
		int n = read(fd, buffer + len, cap - len - 1);
		if (n <= 0) break;
		len += n;
		
		/* Check if we have complete headers (look for \r\n\r\n or \n\n) */
		if (len >= 4 && strstr(buffer, "\r\n\r\n") != nil)
			break;
		if (len >= 2 && strstr(buffer, "\n\n") != nil)
			break;
	}
	
	buffer[len] = '\0';
	
	/* Find start of body (after headers) */
	char* bodyStart = strstr(buffer, "\r\n\r\n");
	if (bodyStart != nil) {
		bodyStart += 4;
	} else {
		bodyStart = strstr(buffer, "\n\n");
		if (bodyStart != nil)
			bodyStart += 2;
		else
			bodyStart = buffer; /* No headers? */
	}
	
	/* Continue reading body if Content-Length specified */
	/* For simplicity, we'll read until connection closes */
	int bodyOffset = bodyStart - buffer;
	while (1) {
		if (len >= cap - 1) {
			cap *= 2;
			char* newbuf = realloc(buffer, cap);
			if (newbuf == nil) {
				free(buffer);
				return nil;
			}
			buffer = newbuf;
		}
		
		int n = read(fd, buffer + len, cap - len - 1);
		if (n <= 0) break;
		len += n;
	}
	
	buffer[len] = '\0';
	
	/* Extract just the body */
	bodyStart = buffer + bodyOffset;
	int bodyLen = len - bodyOffset;
	char* body = malloc(bodyLen + 1);
	if (body == nil) {
		free(buffer);
		return nil;
	}
	memcpy(body, bodyStart, bodyLen);
	body[bodyLen] = '\0';
	
	*outLen = bodyLen;
	free(buffer);
	return body;
}

/* getAwsTimestamp() -> returns instance with amzDate and dateStamp fields
 * amzDate: "20240307T120000Z"
 * dateStamp: "20240307"
 */
static Value
getAwsTimestampNative(int argCount, Value* args)
{
	(void)args;
	if (argCount != 0)
		return NIL_VAL;
	
	Tm* tm = gmtime(time(0));
	
	char amzDate[32];
	char dateStamp[16];
	snprint(amzDate, sizeof(amzDate), "%04d%02d%02dT%02d%02d%02dZ",
		tm->year+1900, tm->mon+1, tm->mday, tm->hour, tm->min, tm->sec);
	snprint(dateStamp, sizeof(dateStamp), "%04d%02d%02d",
		tm->year+1900, tm->mon+1, tm->mday);
	
	/* Create an instance to hold the timestamps */
	ObjString* className = copyString("AwsTimestamp", 12);
	push(OBJ_VAL(className));
	ObjClass* klass = newClass(className);
	pop();
	
	push(OBJ_VAL(klass));
	ObjInstance* instance = newInstance(klass);
	pop();
	
	/* Set fields */
	push(OBJ_VAL(instance));
	ObjString* amzDateKey = copyString("amzDate", 7);
	push(OBJ_VAL(amzDateKey));
	ObjString* amzDateValue = copyString(amzDate, strlen(amzDate));
	push(OBJ_VAL(amzDateValue));
	tableSet(&instance->fields, amzDateKey, OBJ_VAL(amzDateValue));
	pop(); pop();
	
	ObjString* dateStampKey = copyString("dateStamp", 9);
	push(OBJ_VAL(dateStampKey));
	ObjString* dateStampValue = copyString(dateStamp, strlen(dateStamp));
	push(OBJ_VAL(dateStampValue));
	tableSet(&instance->fields, dateStampKey, OBJ_VAL(dateStampValue));
	pop(); pop();
	
	Value result = OBJ_VAL(instance);
	pop(); /* pop instance */
	
	return result;
}

/* httpGet(url) -> string or nil */
static Value 
httpGetNative(int argCount, Value* args)
{
	if (argCount != 1 || !IS_STRING(args[0]))
		return NIL_VAL;
	
	char* url = AS_CSTRING(args[0]);
	UrlParts parts;
	
	if (parseUrl(url, &parts) < 0)
		return NIL_VAL;
	
	int fd;
	TLSconn conn;
	
	/* Dial format: "tcp!host!port" */
	char dialAddr[512];
	snprint(dialAddr, sizeof(dialAddr), "tcp!%s!%s", parts.host, parts.port);
	
	if (parts.ishttps) {
		/* HTTPS: Use TLS with SNI support */
		memset(&conn, 0, sizeof(conn));
		conn.serverName = parts.host;
		fd = dial(dialAddr, nil, nil, nil);
		if (fd < 0)
			return NIL_VAL;
		
		/* Establish TLS connection */
		fd = tlsClient(fd, &conn);
		if (fd < 0) {
			fprint(2, "TLS handshake failed for %s\n", parts.host);
			close(fd);
			return NIL_VAL;
		}
	} else {
		/* HTTP: Plain connection */
		fd = dial(dialAddr, nil, nil, nil);
		if (fd < 0)
			return NIL_VAL;
	}
	
	/* Send HTTP GET request */
	char request[2048];
	snprint(request, sizeof(request),
		"GET %s HTTP/1.0\r\n"
		"Host: %s\r\n"
		"User-Agent: lux/1.0\r\n"
		"Connection: close\r\n"
		"\r\n",
		parts.path, parts.host);
	
	if (writeAll(fd, request, strlen(request)) < 0) {
		if (parts.ishttps)
			free(conn.cert);
		close(fd);
		return NIL_VAL;
	}
	
	/* Read response */
	int bodyLen;
	char* body = readHttpResponse(fd, &bodyLen);
	
	if (parts.ishttps)
		free(conn.cert);
	close(fd);
	
	if (body == nil)
		return NIL_VAL;
	
	Value result = OBJ_VAL(copyString(body, bodyLen));
	free(body);
	return result;
}

typedef struct {
	char* data;
	long size;
	long cap;
} MimeBuf;

typedef struct {
	char* data;
	int size;
	char contentType[128];
} MultipartBody;

static int
mimeBufGrow(MimeBuf* b, long extra)
{
	long need = b->size + extra;
	long cap;
	char* p;

	if (need <= b->cap)
		return 1;
	cap = b->cap == 0 ? 4096 : b->cap;
	while (cap < need) {
		if (cap > 0x3fffffff)
			return 0;
		cap *= 2;
	}
	p = realloc(b->data, cap);
	if (p == nil)
		return 0;
	b->data = p;
	b->cap = cap;
	return 1;
}

static int
mimeBufAppend(MimeBuf* b, const char* s, long n)
{
	if (!mimeBufGrow(b, n))
		return 0;
	if (n > 0)
		memcpy(b->data + b->size, s, n);
	b->size += n;
	return 1;
}

static int
mimeBufAppendCstr(MimeBuf* b, const char* s)
{
	return mimeBufAppend(b, s, strlen(s));
}

static void
mimeBufFree(MimeBuf* b)
{
	free(b->data);
	b->data = nil;
	b->size = 0;
	b->cap = 0;
}

static const char*
pathBasename(const char* path)
{
	const char* slash = strrchr(path, '/');
	if (slash != nil && slash[1] != '\0')
		return slash + 1;
	return path;
}

static const char*
skipAtPrefix(const char* path)
{
	if (path != nil && path[0] == '@')
		return path + 1;
	return path;
}

static int
readFileBytes(const char* path, char** out, long* outLen)
{
	int fd;
	Dir* d;
	long len;
	char* buf;
	long bytesRead;

	fd = open(path, OREAD);
	if (fd < 0)
		return 0;
	d = dirfstat(fd);
	if (d == nil) {
		close(fd);
		return 0;
	}
	len = d->length;
	free(d);
	buf = malloc(len + 1);
	if (buf == nil) {
		close(fd);
		return 0;
	}
	bytesRead = read(fd, buf, len);
	close(fd);
	if (bytesRead < 0) {
		free(buf);
		return 0;
	}
	buf[bytesRead] = '\0';
	*out = buf;
	*outLen = bytesRead;
	return 1;
}

static int
instanceField(ObjInstance* inst, const char* name, Value* out)
{
	return tableGet(&inst->fields, copyString(name, (int)strlen(name)), out);
}

static ObjArray*
multipartPartsFromValue(Value v)
{
	Value partsVal;

	if (IS_ARRAY(v))
		return AS_ARRAY(v);
	if (IS_INSTANCE(v) && instanceField(AS_INSTANCE(v), "parts", &partsVal) &&
	    IS_ARRAY(partsVal))
		return AS_ARRAY(partsVal);
	return nil;
}

static int
partFieldChars(ObjInstance* part, const char* field, const char** chars, int* len)
{
	Value v;

	if (!instanceField(part, field, &v) || IS_NIL(v)) {
		*chars = nil;
		*len = 0;
		return 0;
	}
	if (IS_STRING(v)) {
		*chars = AS_CSTRING(v);
		*len = AS_STRING(v)->length;
		return 1;
	}
	{
		ObjString* s = valueToString(v);
		*chars = s->chars;
		*len = s->length;
		return 1;
	}
}

static int
headerNameIsContentType(const char* convertedName)
{
	return cistrcmp(convertedName, "Content-Type") == 0;
}

/* Build multipart/form-data. Caller frees out->data on success. */
static int
encodeMultipart(ObjArray* parts, MultipartBody* out)
{
	MimeBuf buf;
	char boundary[64];
	int i;

	memset(&buf, 0, sizeof(buf));
	snprint(boundary, sizeof(boundary), "----LuxFormBoundary%ld%d",
		(long)time(0), getpid());
	snprint(out->contentType, sizeof(out->contentType),
		"multipart/form-data; boundary=%s", boundary);

	for (i = 0; i < parts->count; i++) {
		ObjInstance* part;
		const char* name;
		int nameLen;
		const char* path;
		int pathLen;
		const char* value;
		int valueLen;
		const char* filename;
		int filenameLen;
		const char* type;
		int typeLen;
		int isFile;

		if (!IS_INSTANCE(parts->elements[i])) {
			mimeBufFree(&buf);
			return 0;
		}
		part = AS_INSTANCE(parts->elements[i]);
		if (!partFieldChars(part, "name", &name, &nameLen) || nameLen <= 0) {
			mimeBufFree(&buf);
			return 0;
		}

		isFile = partFieldChars(part, "path", &path, &pathLen) && pathLen > 0;
		if (isFile)
			path = skipAtPrefix(path);
		if (!mimeBufAppendCstr(&buf, "--") ||
		    !mimeBufAppendCstr(&buf, boundary) ||
		    !mimeBufAppendCstr(&buf, "\r\n") ||
		    !mimeBufAppendCstr(&buf, "Content-Disposition: form-data; name=\"") ||
		    !mimeBufAppend(&buf, name, nameLen) ||
		    !mimeBufAppendCstr(&buf, "\"")) {
			mimeBufFree(&buf);
			return 0;
		}

		if (isFile) {
			char* fileData = nil;
			long fileLen = 0;
			const char* useName;
			int useNameLen;
			const char* useType;
			int useTypeLen;

			if (partFieldChars(part, "filename", &filename, &filenameLen) &&
			    filenameLen > 0) {
				useName = filename;
				useNameLen = filenameLen;
			} else {
				useName = pathBasename(path);
				useNameLen = (int)strlen(useName);
			}
			if (partFieldChars(part, "type", &type, &typeLen) && typeLen > 0) {
				useType = type;
				useTypeLen = typeLen;
			} else {
				useType = "application/octet-stream";
				useTypeLen = (int)strlen(useType);
			}
			if (!mimeBufAppendCstr(&buf, "; filename=\"") ||
			    !mimeBufAppend(&buf, useName, useNameLen) ||
			    !mimeBufAppendCstr(&buf, "\"\r\nContent-Type: ") ||
			    !mimeBufAppend(&buf, useType, useTypeLen) ||
			    !mimeBufAppendCstr(&buf, "\r\n\r\n")) {
				mimeBufFree(&buf);
				return 0;
			}
			if (!readFileBytes(path, &fileData, &fileLen)) {
				fprint(2, "httpRequest: cannot read file '%s'\n", path);
				mimeBufFree(&buf);
				return 0;
			}
			if (!mimeBufAppend(&buf, fileData, fileLen) ||
			    !mimeBufAppendCstr(&buf, "\r\n")) {
				free(fileData);
				mimeBufFree(&buf);
				return 0;
			}
			free(fileData);
		} else {
			if (!partFieldChars(part, "value", &value, &valueLen)) {
				value = "";
				valueLen = 0;
			}
			if (partFieldChars(part, "type", &type, &typeLen) && typeLen > 0) {
				if (!mimeBufAppendCstr(&buf, "\r\nContent-Type: ") ||
				    !mimeBufAppend(&buf, type, typeLen)) {
					mimeBufFree(&buf);
					return 0;
				}
			}
			if (!mimeBufAppendCstr(&buf, "\r\n\r\n") ||
			    !mimeBufAppend(&buf, value, valueLen) ||
			    !mimeBufAppendCstr(&buf, "\r\n")) {
				mimeBufFree(&buf);
				return 0;
			}
		}
	}

	if (!mimeBufAppendCstr(&buf, "--") ||
	    !mimeBufAppendCstr(&buf, boundary) ||
	    !mimeBufAppendCstr(&buf, "--\r\n")) {
		mimeBufFree(&buf);
		return 0;
	}

	if (buf.data == nil) {
		buf.data = malloc(1);
		if (buf.data == nil)
			return 0;
		buf.data[0] = '\0';
	}
	out->data = buf.data;
	out->size = (int)buf.size;
	return 1;
}

/* httpRequest(method, url, [body], [headers]) -> string or nil
 * method: "GET", "POST", "PUT", "DELETE", etc.
 * url: target URL
 * body: optional request body (nil, string, Form instance, or parts array)
 * headers: optional instance with header fields (nil or instance)
 */
static Value
httpRequestNative(int argCount, Value* args)
{
	char* method;
	char* url;
	char* requestBody;
	int requestBodyLen;
	int bodyOwned;
	int isMultipart;
	char multipartCT[128];
	ObjInstance* headersObj;
	UrlParts parts;
	int fd;
	TLSconn conn;
	char dialAddr[512];
	char request[32768];
	int reqLen;
	int bodyLen;
	char* respBody;
	Value result;

	requestBody = nil;
	requestBodyLen = 0;
	bodyOwned = 0;
	isMultipart = 0;
	headersObj = nil;

	if (argCount < 2 || argCount > 4)
		return NIL_VAL;
	
	if (!IS_STRING(args[0]) || !IS_STRING(args[1]))
		return NIL_VAL;
	
	method = AS_CSTRING(args[0]);
	url = AS_CSTRING(args[1]);
	
	/* Optional body (arg 2): string, Form, or parts array */
	if (argCount >= 3 && !IS_NIL(args[2])) {
		ObjArray* mpParts = multipartPartsFromValue(args[2]);
		if (mpParts != nil) {
			MultipartBody mp;
			if (!encodeMultipart(mpParts, &mp)) {
				fprint(2, "httpRequest: multipart encode failed\n");
				return NIL_VAL;
			}
			requestBody = mp.data;
			requestBodyLen = mp.size;
			bodyOwned = 1;
			isMultipart = 1;
			snprint(multipartCT, sizeof(multipartCT), "%s", mp.contentType);
		} else if (IS_STRING(args[2])) {
			requestBody = AS_CSTRING(args[2]);
			requestBodyLen = AS_STRING(args[2])->length;
		} else {
			return NIL_VAL;
		}
	}
	
	/* Optional headers (arg 3) */
	if (argCount >= 4 && !IS_NIL(args[3])) {
		if (!IS_INSTANCE(args[3])) {
			if (bodyOwned)
				free(requestBody);
			return NIL_VAL;
		}
		headersObj = AS_INSTANCE(args[3]);
	}
	
	if (parseUrl(url, &parts) < 0) {
		if (bodyOwned)
			free(requestBody);
		return NIL_VAL;
	}
	
	/* Dial */
	snprint(dialAddr, sizeof(dialAddr), "tcp!%s!%s", parts.host, parts.port);
	
	if (parts.ishttps) {
		memset(&conn, 0, sizeof(conn));
		conn.serverName = parts.host;
		fd = dial(dialAddr, nil, nil, nil);
		if (fd < 0) {
			if (bodyOwned)
				free(requestBody);
			return NIL_VAL;
		}
		
		fd = tlsClient(fd, &conn);
		if (fd < 0) {
			fprint(2, "TLS handshake failed for %s\n", parts.host);
			close(fd);
			if (bodyOwned)
				free(requestBody);
			return NIL_VAL;
		}
	} else {
		fd = dial(dialAddr, nil, nil, nil);
		if (fd < 0) {
			if (bodyOwned)
				free(requestBody);
			return NIL_VAL;
		}
	}
	
	/* Build HTTP request with custom headers.
	 * STS tokens are ~2KB; keep this large enough for signed AWS calls. */
	reqLen = snprint(request, sizeof(request),
		"%s %s HTTP/1.1\r\n"
		"Host: %s\r\n"
		"User-Agent: lux/1.0\r\n",
		method, parts.path, parts.host);
	
	/* Add custom headers if provided */
	if (headersObj != nil) {
		int i;
		for (i = 0; i < headersObj->fields.capacity; i++) {
			if (headersObj->fields.entries[i].key != nil) {
				char* headerName = headersObj->fields.entries[i].key->chars;
				Value headerValue = headersObj->fields.entries[i].value;
				
				if (IS_STRING(headerValue)) {
					char convertedName[256];
					char* p;
					strncpy(convertedName, headerName, sizeof(convertedName) - 1);
					convertedName[sizeof(convertedName) - 1] = '\0';
					for (p = convertedName; *p; p++) {
						if (*p == '_') *p = '-';
					}
					if (isMultipart && headerNameIsContentType(convertedName))
						continue;
					
					reqLen += snprint(request + reqLen, sizeof(request) - reqLen,
						"%s: %s\r\n", convertedName, AS_CSTRING(headerValue));
				}
			}
		}
	}

	if (isMultipart) {
		reqLen += snprint(request + reqLen, sizeof(request) - reqLen,
			"Content-Type: %s\r\n", multipartCT);
	}
	
	/* Add Content-Length if body is present (string length, not strlen) */
	if (requestBody != nil && requestBodyLen > 0) {
		reqLen += snprint(request + reqLen, sizeof(request) - reqLen,
			"Content-Length: %d\r\n", requestBodyLen);
	}
	
	/* Close headers */
	reqLen += snprint(request + reqLen, sizeof(request) - reqLen,
		"Connection: close\r\n\r\n");
	
	/* Write headers */
	if (writeAll(fd, request, reqLen) < 0) {
		if (parts.ishttps)
			free(conn.cert);
		close(fd);
		if (bodyOwned)
			free(requestBody);
		return NIL_VAL;
	}
	
	/* Write body if present */
	if (requestBody != nil && requestBodyLen > 0) {
		if (writeAll(fd, requestBody, requestBodyLen) < 0) {
			if (parts.ishttps)
				free(conn.cert);
			close(fd);
			if (bodyOwned)
				free(requestBody);
			return NIL_VAL;
		}
	}

	if (bodyOwned)
		free(requestBody);
	
	/* Read response */
	respBody = readHttpResponse(fd, &bodyLen);
	
	if (parts.ishttps)
		free(conn.cert);
	close(fd);
	
	if (respBody == nil)
		return NIL_VAL;
	
	result = OBJ_VAL(copyString(respBody, bodyLen));
	free(respBody);
	return result;
}

/* httpPostForm(url, parts, [headers]) -> string or nil */
static Value
httpPostFormNative(int argCount, Value* args)
{
	Value reqArgs[4];
	Value method;
	Value result;

	if (argCount < 2 || argCount > 3)
		return NIL_VAL;
	if (!IS_STRING(args[0]))
		return NIL_VAL;

	method = OBJ_VAL(copyString("POST", 4));
	push(method);
	reqArgs[0] = method;
	reqArgs[1] = args[0];
	reqArgs[2] = args[1];
	if (argCount >= 3) {
		reqArgs[3] = args[2];
		result = httpRequestNative(4, reqArgs);
	} else {
		result = httpRequestNative(3, reqArgs);
	}
	pop();
	return result;
}

/* httpPost(url, body) -> string or nil */
static Value 
httpPostNative(int argCount, Value* args)
{
	if (argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1]))
		return NIL_VAL;
	
	char* url = AS_CSTRING(args[0]);
	char* postBody = AS_CSTRING(args[1]);
	int postBodyLen = strlen(postBody);
	
	UrlParts parts;
	if (parseUrl(url, &parts) < 0)
		return NIL_VAL;
	
	int fd;
	TLSconn conn;
	
	/* Dial */
	char dialAddr[512];
	snprint(dialAddr, sizeof(dialAddr), "tcp!%s!%s", parts.host, parts.port);
	
	if (parts.ishttps) {
		/* HTTPS: Use TLS with SNI support */
		memset(&conn, 0, sizeof(conn));
		conn.serverName = parts.host;
		fd = dial(dialAddr, nil, nil, nil);
		if (fd < 0)
			return NIL_VAL;
		
		fd = tlsClient(fd, &conn);
		if (fd < 0) {
			fprint(2, "TLS handshake failed for %s\n", parts.host);
			close(fd);
			return NIL_VAL;
		}
	} else {
		/* HTTP: Plain connection */
		fd = dial(dialAddr, nil, nil, nil);
		if (fd < 0)
			return NIL_VAL;
	}
	
	/* Send HTTP POST request */
	char request[16384];
	int reqLen = snprint(request, sizeof(request),
		"POST %s HTTP/1.0\r\n"
		"Host: %s\r\n"
		"User-Agent: lux/1.0\r\n"
		"Content-Type: application/json\r\n"
		"Content-Length: %d\r\n"
		"Connection: close\r\n"
		"\r\n",
		parts.path, parts.host, postBodyLen);
	
	if (writeAll(fd, request, reqLen) < 0) {
		if (parts.ishttps)
			free(conn.cert);
		close(fd);
		return NIL_VAL;
	}
	
	if (writeAll(fd, postBody, postBodyLen) < 0) {
		if (parts.ishttps)
			free(conn.cert);
		close(fd);
		return NIL_VAL;
	}
	
	/* Read response */
	int bodyLen;
	char* respBody = readHttpResponse(fd, &bodyLen);
	
	if (parts.ishttps)
		free(conn.cert);
	close(fd);
	
	if (respBody == nil)
		return NIL_VAL;
	
	Value result = OBJ_VAL(copyString(respBody, bodyLen));
	free(respBody);
	return result;
}

/* httpPut(url, body) -> string or nil */
static Value 
httpPutNative(int argCount, Value* args)
{
	if (argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1]))
		return NIL_VAL;
	
	char* url = AS_CSTRING(args[0]);
	char* putBody = AS_CSTRING(args[1]);
	int putBodyLen = strlen(putBody);
	
	UrlParts parts;
	if (parseUrl(url, &parts) < 0)
		return NIL_VAL;
	
	int fd;
	TLSconn conn;
	
	/* Dial */
	char dialAddr[512];
	snprint(dialAddr, sizeof(dialAddr), "tcp!%s!%s", parts.host, parts.port);
	
	if (parts.ishttps) {
		/* HTTPS: Use TLS with SNI support */
		memset(&conn, 0, sizeof(conn));
		conn.serverName = parts.host;
		fd = dial(dialAddr, nil, nil, nil);
		if (fd < 0)
			return NIL_VAL;
		
		fd = tlsClient(fd, &conn);
		if (fd < 0) {
			fprint(2, "TLS handshake failed for %s\n", parts.host);
			close(fd);
			return NIL_VAL;
		}
	} else {
		/* HTTP: Plain connection */
		fd = dial(dialAddr, nil, nil, nil);
		if (fd < 0)
			return NIL_VAL;
	}
	
	/* Send HTTP PUT request */
	char request[16384];
	int reqLen = snprint(request, sizeof(request),
		"PUT %s HTTP/1.0\r\n"
		"Host: %s\r\n"
		"User-Agent: lux/1.0\r\n"
		"Content-Type: application/json\r\n"
		"Content-Length: %d\r\n"
		"Connection: close\r\n"
		"\r\n",
		parts.path, parts.host, putBodyLen);
	
	if (writeAll(fd, request, reqLen) < 0) {
		if (parts.ishttps)
			free(conn.cert);
		close(fd);
		return NIL_VAL;
	}
	
	if (writeAll(fd, putBody, putBodyLen) < 0) {
		if (parts.ishttps)
			free(conn.cert);
		close(fd);
		return NIL_VAL;
	}
	
	/* Read response */
	int bodyLen;
	char* respBody = readHttpResponse(fd, &bodyLen);
	
	if (parts.ishttps)
		free(conn.cert);
	close(fd);
	
	if (respBody == nil)
		return NIL_VAL;
	
	Value result = OBJ_VAL(copyString(respBody, bodyLen));
	free(respBody);
	return result;
}

/* HTTP Request parsing for server */
typedef struct {
	char method[16];
	char path[1024];
	char host[256];
	char body[4096];
	int bodyLen;
} HttpRequest;

/* Strip :port from Host value; keep [ipv6] without trailing :port. */
static void
httpStripHostPort(char* host)
{
	char* p;
	char* q;

	if (host[0] == '\0')
		return;
	if (host[0] == '[') {
		p = strchr(host, ']');
		if (p != nil && p[1] == ':')
			p[1] = '\0';
		return;
	}
	p = strrchr(host, ':');
	if (p == nil || p[1] == '\0')
		return;
	for (q = p + 1; *q; q++) {
		if (*q < '0' || *q > '9')
			return;
	}
	*p = '\0';
}

static int
httpHeaderNameIs(char* line, char* name)
{
	int i;
	for (i = 0; name[i] != '\0'; i++) {
		char a = line[i];
		char b = name[i];
		if (a >= 'A' && a <= 'Z') a = a - 'A' + 'a';
		if (b >= 'A' && b <= 'Z') b = b - 'A' + 'a';
		if (a != b) return 0;
	}
	return line[i] == ':';
}

static int
parseHttpRequest(char* buffer, int bufLen, HttpRequest* req)
{
	char* p = buffer;
	char* bodyStart;
	char* line;
	char* endHeaders;
	
	req->host[0] = '\0';

	/* Parse method (GET, POST, etc.) */
	char* methodEnd = strchr(p, ' ');
	if (methodEnd == nil) return -1;
	int methodLen = methodEnd - p;
	if (methodLen >= 16) return -1;
	strncpy(req->method, p, methodLen);
	req->method[methodLen] = '\0';
	
	/* Parse path */
	p = methodEnd + 1;
	char* pathEnd = strchr(p, ' ');
	if (pathEnd == nil) return -1;
	int pathLen = pathEnd - p;
	if (pathLen >= 1024) return -1;
	strncpy(req->path, p, pathLen);
	req->path[pathLen] = '\0';
	
	/* Find body (after headers) */
	bodyStart = strstr(buffer, "\r\n\r\n");
	if (bodyStart != nil) {
		bodyStart += 4;
	} else {
		bodyStart = strstr(buffer, "\n\n");
		if (bodyStart != nil)
			bodyStart += 2;
		else
			bodyStart = nil;
	}

	/* Parse Host header */
	endHeaders = bodyStart != nil ? bodyStart : buffer + bufLen;
	line = strchr(buffer, '\n');
	if (line != nil) line++;
	while (line != nil && line < endHeaders) {
		char* next = strchr(line, '\n');
		char* lineEnd = next != nil ? next : endHeaders;
		if (lineEnd > line && lineEnd[-1] == '\r')
			lineEnd--;
		if (httpHeaderNameIs(line, "host")) {
			char* v = line + 5;
			int hostLen;
			while (v < lineEnd && (*v == ' ' || *v == '\t'))
				v++;
			hostLen = lineEnd - v;
			if (hostLen >= 256) hostLen = 255;
			if (hostLen > 0) {
				strncpy(req->host, v, hostLen);
				req->host[hostLen] = '\0';
				httpStripHostPort(req->host);
			}
			break;
		}
		if (next == nil) break;
		line = next + 1;
	}
	
	/* Extract body if present */
	if (bodyStart != nil) {
		req->bodyLen = bufLen - (bodyStart - buffer);
		if (req->bodyLen >= 4096) req->bodyLen = 4095;
		memcpy(req->body, bodyStart, req->bodyLen);
		req->body[req->bodyLen] = '\0';
	} else {
		req->body[0] = '\0';
		req->bodyLen = 0;
	}
	
	return 0;
}

static void sendHttpResponseEx(int fd, int statusCode, char* statusText, char* contentType, char* body);
static void sendHttpResponseBinary(int fd, int statusCode, char* statusText, char* contentType, void* body, int bodyLen);

static void
sendHttpResponse(int fd, int statusCode, char* statusText, char* body)
{
	sendHttpResponseEx(fd, statusCode, statusText, "application/json", body);
}

static void
sendHttpResponseEx(int fd, int statusCode, char* statusText, char* contentType, char* body)
{
	int bodyLen;

	if (body == nil)
		body = "";
	bodyLen = strlen(body);
	sendHttpResponseBinary(fd, statusCode, statusText, contentType, body, bodyLen);
}

static void
sendHttpResponseBinary(int fd, int statusCode, char* statusText, char* contentType, void* body, int bodyLen)
{
	char header[512];
	int headerLen;

	if (statusText == nil)
		statusText = "OK";
	if (contentType == nil)
		contentType = "text/plain";
	if (body == nil)
		bodyLen = 0;
	if (bodyLen < 0)
		bodyLen = 0;
	headerLen = snprint(header, sizeof(header),
		"HTTP/1.0 %d %s\r\n"
		"Content-Type: %s\r\n"
		"Content-Length: %d\r\n"
		"Server: lux/1.0\r\n"
		"Connection: close\r\n"
		"\r\n",
		statusCode, statusText, contentType, bodyLen);

	if (headerLen > 0)
		write(fd, header, headerLen);
	if (bodyLen > 0 && body != nil)
		write(fd, body, bodyLen);
}

static char*
getMimeType(char* path)
{
	char* ext = strrchr(path, '.');
	if (ext == nil) return "application/octet-stream";
	ext++;
	if (cistrcmp(ext, "html") == 0) return "text/html";
	if (cistrcmp(ext, "htm") == 0) return "text/html";
	if (cistrcmp(ext, "css") == 0) return "text/css";
	if (cistrcmp(ext, "js") == 0) return "application/javascript";
	if (cistrcmp(ext, "json") == 0) return "application/json";
	if (cistrcmp(ext, "txt") == 0) return "text/plain";
	if (cistrcmp(ext, "png") == 0) return "image/png";
	if (cistrcmp(ext, "jpg") == 0) return "image/jpeg";
	if (cistrcmp(ext, "jpeg") == 0) return "image/jpeg";
	if (cistrcmp(ext, "gif") == 0) return "image/gif";
	if (cistrcmp(ext, "svg") == 0) return "image/svg+xml";
	return "application/octet-stream";
}

/* ========== HTTP Server: Req/Res objects and Server class ========== */

static ObjClass* serverResClass;
static ObjClass* serverClass;
static ObjClass* dictClass;
static ObjClass* fileClass;
static ObjClass* ninepClass;
static ObjClass* ninepConnClass;

static bool call(ObjClosure* closure, int argCount);
static bool callValue(Value callee, int argCount);
static InterpretResult run(void);

static char*
httpStatusText(int statusCode)
{
	if (statusCode == 200) return "OK";
	if (statusCode == 201) return "Created";
	if (statusCode == 400) return "Bad Request";
	if (statusCode == 404) return "Not Found";
	if (statusCode == 500) return "Internal Server Error";
	return "OK";
}

static int
resGetFd(ObjInstance* res)
{
	Value fdVal;

	if (!tableGet(&res->fields, copyString("_fd", 3), &fdVal) || !IS_NUMBER(fdVal))
		return -1;
	return (int)AS_NUMBER(fdVal);
}

static void
resEmit(ObjInstance* res, int statusCode, char* contentType, void* body, int bodyLen)
{
	int fd;

	fd = resGetFd(res);
	if (contentType == nil)
		contentType = "text/plain";
	if (body == nil) {
		body = "";
		bodyLen = 0;
	}
	if (bodyLen < 0)
		bodyLen = 0;
	tableSet(&res->fields, copyString("_statusCode", 11), NUMBER_VAL((double)statusCode));
	if (fd >= 0) {
		sendHttpResponseBinary(fd, statusCode, httpStatusText(statusCode),
			contentType, body, bodyLen);
		return;
	}
	tableSet(&res->fields, copyString("_contentType", 12),
		OBJ_VAL(copyString(contentType, (int)strlen(contentType))));
	tableSet(&res->fields, copyString("_body", 5),
		OBJ_VAL(copyString((char*)body, bodyLen)));
}

/* res.status(code) -> returns res for chaining */
static Value
resStatusNative(int argCount, Value* args)
{
	if (argCount != 2 || !IS_INSTANCE(args[0]) || !IS_NUMBER(args[1]))
		return NIL_VAL;
	ObjInstance* res = AS_INSTANCE(args[0]);
	ObjString* key = copyString("_statusCode", 11);
	tableSet(&res->fields, key, args[1]);
	return args[0];
}

/* res.send(body) */
static Value
resSendNative(int argCount, Value* args)
{
	Value statusVal;
	int statusCode;
	ObjString* bodyObj;
	ObjInstance* res;

	if (argCount != 2 || !IS_INSTANCE(args[0]))
		return NIL_VAL;
	res = AS_INSTANCE(args[0]);
	statusCode = 200;
	if (tableGet(&res->fields, copyString("_statusCode", 11), &statusVal) && IS_NUMBER(statusVal))
		statusCode = (int)AS_NUMBER(statusVal);
	bodyObj = valueToString(args[1]);
	resEmit(res, statusCode, "text/plain", bodyObj->chars, bodyObj->length);
	return NIL_VAL;
}

/* res.html(body) */
static Value
resHtmlNative(int argCount, Value* args)
{
	Value statusVal;
	int statusCode;
	ObjString* bodyObj;
	ObjInstance* res;

	if (argCount != 2 || !IS_INSTANCE(args[0]))
		return NIL_VAL;
	res = AS_INSTANCE(args[0]);
	statusCode = 200;
	if (tableGet(&res->fields, copyString("_statusCode", 11), &statusVal) && IS_NUMBER(statusVal))
		statusCode = (int)AS_NUMBER(statusVal);
	bodyObj = valueToString(args[1]);
	resEmit(res, statusCode, "text/html; charset=utf-8", bodyObj->chars, bodyObj->length);
	return NIL_VAL;
}

/* res.next() - advances middleware chain */
static ObjArray* _mwChainMiddlewares;
static int _mwChainIndex;
static ObjClosure* _mwChainHandler;
static Value _mwChainReq;
static Value _mwChainRes;

static Value
resNextNative(int argCount, Value* args)
{
	Value* savedTop;
	(void)argCount;
	(void)args;
	if (_mwChainMiddlewares == nil) return NIL_VAL;
	_mwChainIndex++;
	if (_mwChainIndex < _mwChainMiddlewares->count) {
		Value mwVal = _mwChainMiddlewares->elements[_mwChainIndex];
		if (IS_CLOSURE(mwVal)) {
			savedTop = vm.stackTop;
			push(OBJ_VAL(mwVal));
			push(_mwChainReq);
			push(_mwChainRes);
			push(OBJ_VAL(newNative(resNextNative)));
			if (call(AS_CLOSURE(mwVal), 3)) {
				run();
			}
			vm.stackTop = savedTop;
		}
	} else if (_mwChainHandler != nil) {
		ObjClosure* h = _mwChainHandler;
		_mwChainHandler = nil;
		_mwChainMiddlewares = nil;
		savedTop = vm.stackTop;
		push(OBJ_VAL(h));
		push(_mwChainReq);
		push(_mwChainRes);
		if (call(h, 2)) {
			run();
		}
		vm.stackTop = savedTop;
	}
	return NIL_VAL;
}

/* res.json(obj) */
static Value
resJsonNative(int argCount, Value* args)
{
	int statusCode, jsonCap, len;
	Value statusVal;
	ObjInstance* res;
	char* buffer;

	if (argCount != 2 || !IS_INSTANCE(args[0]))
		return NIL_VAL;
	res = AS_INSTANCE(args[0]);
	statusCode = 200;
	if (tableGet(&res->fields, copyString("_statusCode", 11), &statusVal) && IS_NUMBER(statusVal))
		statusCode = (int)AS_NUMBER(statusVal);
	jsonCap = 256;
	len = 0;
	buffer = malloc((ulong)jsonCap);
	if (buffer == nil) return NIL_VAL;
	buffer[0] = '\0';
	serializeJsonValue(args[1], &buffer, &len, &jsonCap);
	resEmit(res, statusCode, "application/json", buffer, len);
	free(buffer);
	return NIL_VAL;
}

/* Helper: get or create _routes array on server instance */
static ObjArray*
serverGetRoutes(ObjInstance* server, char* key)
{
	Value routesVal;
	ObjString* routesKey = copyString(key, (int)strlen(key));
	if (!tableGet(&server->fields, routesKey, &routesVal)) {
		ObjArray* arr = newArray();
		tableSet(&server->fields, routesKey, OBJ_VAL(arr));
		return arr;
	}
	if (!IS_ARRAY(routesVal)) return nil;
	return AS_ARRAY(routesVal);
}

/* Server.init(port) - returns instance for constructor */
static Value
serverInitNative(int argCount, Value* args)
{
	if (argCount != 2 || !IS_INSTANCE(args[0]) || !IS_NUMBER(args[1]))
		return NIL_VAL;
	ObjInstance* server = AS_INSTANCE(args[0]);
	ObjString* portKey = copyString("port", 4);
	tableSet(&server->fields, portKey, args[1]);
	return args[0];
}

/* Server.get(path, handler) — global route (any Host) */
static Value
serverGetNative(int argCount, Value* args)
{
	if (argCount != 3 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1]) || !IS_CLOSURE(args[2]))
		return NIL_VAL;
	ObjInstance* server = AS_INSTANCE(args[0]);
	ObjArray* routes = serverGetRoutes(server, "_routes");
	if (routes == nil) return NIL_VAL;
	ObjInstance* entry = newInstance(nil);
	push(OBJ_VAL(entry));
	ObjString* methodKey = copyString("method", 6);
	ObjString* pathKey = copyString("path", 4);
	ObjString* handlerKey = copyString("handler", 7);
	tableSet(&entry->fields, methodKey, OBJ_VAL(copyString("GET", 3)));
	tableSet(&entry->fields, pathKey, args[1]);
	tableSet(&entry->fields, handlerKey, args[2]);
	writeArray(routes, OBJ_VAL(entry));
	pop();
	return NIL_VAL;
}

/* Server.post(path, handler) — global route (any Host) */
static Value
serverPostNative(int argCount, Value* args)
{
	if (argCount != 3 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1]) || !IS_CLOSURE(args[2]))
		return NIL_VAL;
	ObjInstance* server = AS_INSTANCE(args[0]);
	ObjArray* routes = serverGetRoutes(server, "_routes");
	if (routes == nil) return NIL_VAL;
	ObjInstance* entry = newInstance(nil);
	push(OBJ_VAL(entry));
	ObjString* methodKey = copyString("method", 6);
	ObjString* pathKey = copyString("path", 4);
	ObjString* handlerKey = copyString("handler", 7);
	tableSet(&entry->fields, methodKey, OBJ_VAL(copyString("POST", 4)));
	tableSet(&entry->fields, pathKey, args[1]);
	tableSet(&entry->fields, handlerKey, args[2]);
	writeArray(routes, OBJ_VAL(entry));
	pop();
	return NIL_VAL;
}

/* Server.getHost(host, path, handler) */
static Value
serverGetHostNative(int argCount, Value* args)
{
	if (argCount != 4 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1]) ||
	    !IS_STRING(args[2]) || !IS_CLOSURE(args[3]))
		return NIL_VAL;
	ObjInstance* server = AS_INSTANCE(args[0]);
	ObjArray* routes = serverGetRoutes(server, "_routes");
	if (routes == nil) return NIL_VAL;
	ObjInstance* entry = newInstance(nil);
	push(OBJ_VAL(entry));
	tableSet(&entry->fields, copyString("method", 6), OBJ_VAL(copyString("GET", 3)));
	tableSet(&entry->fields, copyString("host", 4), args[1]);
	tableSet(&entry->fields, copyString("path", 4), args[2]);
	tableSet(&entry->fields, copyString("handler", 7), args[3]);
	writeArray(routes, OBJ_VAL(entry));
	pop();
	return NIL_VAL;
}

/* Server.postHost(host, path, handler) */
static Value
serverPostHostNative(int argCount, Value* args)
{
	if (argCount != 4 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1]) ||
	    !IS_STRING(args[2]) || !IS_CLOSURE(args[3]))
		return NIL_VAL;
	ObjInstance* server = AS_INSTANCE(args[0]);
	ObjArray* routes = serverGetRoutes(server, "_routes");
	if (routes == nil) return NIL_VAL;
	ObjInstance* entry = newInstance(nil);
	push(OBJ_VAL(entry));
	tableSet(&entry->fields, copyString("method", 6), OBJ_VAL(copyString("POST", 4)));
	tableSet(&entry->fields, copyString("host", 4), args[1]);
	tableSet(&entry->fields, copyString("path", 4), args[2]);
	tableSet(&entry->fields, copyString("handler", 7), args[3]);
	writeArray(routes, OBJ_VAL(entry));
	pop();
	return NIL_VAL;
}

/* Server.vhost(host, root) — map Host to static document root */
static Value
serverVhostNative(int argCount, Value* args)
{
	if (argCount != 3 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1]) || !IS_STRING(args[2]))
		return NIL_VAL;
	ObjInstance* server = AS_INSTANCE(args[0]);
	ObjArray* vhosts = serverGetRoutes(server, "_vhosts");
	if (vhosts == nil) return NIL_VAL;
	ObjInstance* entry = newInstance(nil);
	push(OBJ_VAL(entry));
	tableSet(&entry->fields, copyString("host", 4), args[1]);
	tableSet(&entry->fields, copyString("root", 4), args[2]);
	writeArray(vhosts, OBJ_VAL(entry));
	pop();
	return NIL_VAL;
}

/* Lookup static root for Host from _vhosts; nil if none. */
static char*
serverLookupVhostRoot(ObjInstance* server, char* host)
{
	Value vhostsVal;
	ObjArray* vhosts;
	int i;

	if (host == nil || host[0] == '\0')
		return nil;
	if (!tableGet(&server->fields, copyString("_vhosts", 7), &vhostsVal) || !IS_ARRAY(vhostsVal))
		return nil;
	vhosts = AS_ARRAY(vhostsVal);
	for (i = 0; i < vhosts->count; i++) {
		Value entVal = vhosts->elements[i];
		Value hostVal, rootVal;
		if (!IS_INSTANCE(entVal)) continue;
		if (!tableGet(&AS_INSTANCE(entVal)->fields, copyString("host", 4), &hostVal)) continue;
		if (!tableGet(&AS_INSTANCE(entVal)->fields, copyString("root", 4), &rootVal)) continue;
		if (!IS_STRING(hostVal) || !IS_STRING(rootVal)) continue;
		if (cistrcmp(AS_CSTRING(hostVal), host) == 0)
			return AS_CSTRING(rootVal);
	}
	return nil;
}

static ObjClosure*
serverFindHandler(ObjArray* routes, char* method, char* path, char* host)
{
	ObjClosure* handler;
	int pass, i, hasHost;
	Value entVal, methodVal, pathVal, handlerVal, hostVal;
	ObjInstance* ent;

	handler = nil;
	if (routes == nil || method == nil || path == nil)
		return nil;
	if (host == nil)
		host = "";
	for (pass = 0; pass < 2 && handler == nil; pass++) {
		for (i = 0; i < routes->count; i++) {
			entVal = routes->elements[i];
			if (!IS_INSTANCE(entVal)) continue;
			ent = AS_INSTANCE(entVal);
			if (!tableGet(&ent->fields, copyString("method", 6), &methodVal)) continue;
			if (!tableGet(&ent->fields, copyString("path", 4), &pathVal)) continue;
			if (!tableGet(&ent->fields, copyString("handler", 7), &handlerVal)) continue;
			if (!IS_STRING(methodVal) || !IS_STRING(pathVal) || !IS_CLOSURE(handlerVal)) continue;
			hasHost = tableGet(&ent->fields, copyString("host", 4), &hostVal) && IS_STRING(hostVal);
			if (pass == 0) {
				if (!hasHost) continue;
				if (cistrcmp(AS_CSTRING(hostVal), host) != 0) continue;
			} else {
				if (hasHost) continue;
			}
			if (strcmp(AS_CSTRING(methodVal), method) != 0) continue;
			if (strcmp(AS_CSTRING(pathVal), path) != 0) continue;
			handler = AS_CLOSURE(handlerVal);
			break;
		}
	}
	return handler;
}

static int
serverTryStatic(ObjInstance* server, ObjInstance* res, char* method, char* path, char* host)
{
	Value staticVal;
	char* staticDir;
	char filepath[2048];
	int dirLen, pathLen, ffd, nread;
	long fileLen;
	Dir* d;
	char* content;
	char* mime;
	char* reqPath;

	if (method == nil || strcmp(method, "GET") != 0)
		return 0;
	staticDir = serverLookupVhostRoot(server, host);
	if (staticDir == nil) {
		if (!tableGet(&server->fields, copyString("_static_dir", 11), &staticVal) || !IS_STRING(staticVal))
			return 0;
		staticDir = AS_CSTRING(staticVal);
	}
	reqPath = (path != nil) ? path : "/";
	if (strstr(reqPath, "..") != nil)
		return 0;
	dirLen = strlen(staticDir);
	pathLen = strlen(reqPath);
	if (dirLen + pathLen + 12 >= sizeof(filepath))
		return 0;
	snprint(filepath, sizeof(filepath), "%s%s", staticDir, reqPath);
	if (pathLen > 0 && (reqPath[pathLen - 1] == '/' || (pathLen == 1 && reqPath[0] == '/')))
		strncat(filepath, "index.html", sizeof(filepath) - strlen(filepath) - 1);
	ffd = open(filepath, OREAD);
	if (ffd < 0)
		return 0;
	d = dirfstat(ffd);
	if (d == nil) {
		close(ffd);
		return 0;
	}
	fileLen = d->length;
	free(d);
	if (fileLen <= 0 || fileLen >= (long)(16*1024*1024)) {
		resEmit(res, 500, "application/json",
			"{\"error\":\"file too large or empty\"}", 36);
		close(ffd);
		return 1;
	}
	content = malloc((ulong)fileLen);
	if (content == nil) {
		resEmit(res, 500, "application/json",
			"{\"error\":\"out of memory\"}", 26);
		close(ffd);
		return 1;
	}
	nread = read(ffd, content, (int)fileLen);
	mime = getMimeType(filepath);
	if (nread < 0)
		nread = 0;
	resEmit(res, 200, mime, content, nread);
	free(content);
	close(ffd);
	return 1;
}

static void
serverDispatch(ObjInstance* server, char* method, char* path, char* host, char* body, ObjInstance* resObj)
{
	ObjInstance* reqObj;
	Value reqVal, resVal, mwVal, routesVal, nextFn;
	ObjArray* middlewares;
	ObjArray* routes;
	ObjClosure* handler;
	Value* savedTop;
	char errBody[256];

	if (method == nil) method = "GET";
	if (path == nil || path[0] == '\0') path = "/";
	if (host == nil) host = "";
	if (body == nil) body = "";

	middlewares = nil;
	routes = nil;
	reqObj = newInstance(nil);
	push(OBJ_VAL(reqObj));
	tableSet(&reqObj->fields, copyString("method", 6), OBJ_VAL(copyString(method, (int)strlen(method))));
	tableSet(&reqObj->fields, copyString("path", 4), OBJ_VAL(copyString(path, (int)strlen(path))));
	tableSet(&reqObj->fields, copyString("host", 4), OBJ_VAL(copyString(host, (int)strlen(host))));
	tableSet(&reqObj->fields, copyString("body", 4), OBJ_VAL(copyString(body, (int)strlen(body))));

	if (tableGet(&server->fields, copyString("_middleware", 11), &mwVal) && IS_ARRAY(mwVal))
		middlewares = AS_ARRAY(mwVal);
	if (tableGet(&server->fields, copyString("_routes", 7), &routesVal) && IS_ARRAY(routesVal))
		routes = AS_ARRAY(routesVal);

	handler = serverFindHandler(routes, method, path, host);
	reqVal = OBJ_VAL(reqObj);
	resVal = OBJ_VAL(resObj);
	nextFn = NIL_VAL;
	if (handler != nil && middlewares != nil && middlewares->count > 0)
		nextFn = OBJ_VAL(newNative(resNextNative));

	pop(); /* req */

	if (handler != nil) {
		savedTop = vm.stackTop;
		if (middlewares != nil && middlewares->count > 0) {
			Value firstMw;
			_mwChainMiddlewares = middlewares;
			_mwChainIndex = 0;
			_mwChainHandler = handler;
			_mwChainReq = reqVal;
			_mwChainRes = resVal;
			firstMw = middlewares->elements[0];
			push(firstMw);
			push(reqVal);
			push(resVal);
			push(nextFn);
			if (call(AS_CLOSURE(firstMw), 3))
				run();
		} else {
			push(OBJ_VAL(handler));
			push(reqVal);
			push(resVal);
			if (call(handler, 2))
				run();
		}
		vm.stackTop = savedTop;
	} else if (!serverTryStatic(server, resObj, method, path, host)) {
		snprint(errBody, sizeof(errBody),
			"{\"error\":\"Not Found\",\"path\":\"%s\"}", path);
		resEmit(resObj, 404, "application/json", errBody, (int)strlen(errBody));
	}
}

/* Server.use(middleware) */
static Value
serverUseNative(int argCount, Value* args)
{
	if (argCount != 2 || !IS_INSTANCE(args[0]) || !IS_CLOSURE(args[1]))
		return NIL_VAL;
	ObjInstance* server = AS_INSTANCE(args[0]);
	Value mwVal;
	ObjString* mwKey = copyString("_middleware", 11);
	if (!tableGet(&server->fields, mwKey, &mwVal)) {
		ObjArray* arr = newArray();
		tableSet(&server->fields, mwKey, OBJ_VAL(arr));
		writeArray(arr, args[1]);
	} else if (IS_ARRAY(mwVal)) {
		writeArray(AS_ARRAY(mwVal), args[1]);
	}
	return NIL_VAL;
}

/* Server.static(dir) */
static Value
serverStaticNative(int argCount, Value* args)
{
	if (argCount != 2 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1]))
		return NIL_VAL;
	ObjInstance* server = AS_INSTANCE(args[0]);
	ObjString* key = copyString("_static_dir", 11);
	tableSet(&server->fields, key, args[1]);
	return NIL_VAL;
}

/* Server.workers(n) — prefork worker count (1..32); returns server for chaining */
static Value
serverWorkersNative(int argCount, Value* args)
{
	int n;

	if (argCount != 2 || !IS_INSTANCE(args[0]) || !IS_NUMBER(args[1]))
		return NIL_VAL;
	n = (int)AS_NUMBER(args[1]);
	if (n < 1)
		n = 1;
	if (n > 32)
		n = 32;
	tableSet(&AS_INSTANCE(args[0])->fields, copyString("_workers", 8), NUMBER_VAL((double)n));
	return args[0];
}

static int
serverGetWorkerCount(ObjInstance* server)
{
	Value wVal;
	int n;

	if (!tableGet(&server->fields, copyString("_workers", 8), &wVal) || !IS_NUMBER(wVal))
		return 4;
	n = (int)AS_NUMBER(wVal);
	if (n < 1)
		return 1;
	if (n > 32)
		return 32;
	return n;
}

/* Handle one accepted client connection (does not close dfd). */
static void
serverHandleClient(ObjInstance* server, int dfd, ObjArray* routes, char* defaultStaticDir)
{
	char buffer[8192];
	int totalRead;
	int n;
	HttpRequest req;
	ObjInstance* resObj;

	USED(routes);
	USED(defaultStaticDir);

	n = read(dfd, buffer, sizeof(buffer) - 1);
	if (n <= 0)
		return;
	totalRead = n;
	buffer[totalRead] = '\0';

	{
		char* headerEnd = strstr(buffer, "\r\n\r\n");
		if (headerEnd == nil)
			headerEnd = strstr(buffer, "\n\n");
		if (headerEnd != nil) {
			char* clHeader = strstr(buffer, "Content-Length:");
			if (clHeader == nil)
				clHeader = strstr(buffer, "content-length:");
			if (clHeader != nil) {
				int contentLen = atoi(clHeader + 15);
				int bodyStart = (headerEnd - buffer) + 4;
				if (strstr(buffer, "\n\n") != nil && strstr(buffer, "\r\n\r\n") == nil)
					bodyStart = (headerEnd - buffer) + 2;
				int bodyReceived = totalRead - bodyStart;
				int needMore = contentLen - bodyReceived;
				while (needMore > 0 && totalRead < sizeof(buffer) - 1) {
					n = read(dfd, buffer + totalRead, sizeof(buffer) - totalRead - 1);
					if (n <= 0) break;
					totalRead += n;
					needMore -= n;
				}
				buffer[totalRead] = '\0';
			}
		}
	}

	if (parseHttpRequest(buffer, totalRead, &req) < 0) {
		sendHttpResponse(dfd, 400, "Bad Request", "{\"error\":\"Bad Request\"}");
		return;
	}
	fprint(1, "%s %s Host:%s\n", req.method, req.path, req.host);

	resObj = newInstance(serverResClass);
	push(OBJ_VAL(resObj));
	tableSet(&resObj->fields, copyString("_fd", 3), NUMBER_VAL((double)dfd));
	tableSet(&resObj->fields, copyString("_statusCode", 11), NUMBER_VAL(200));
	serverDispatch(server, req.method, req.path, req.host, req.body, resObj);
	pop(); /* res */
}

static void
httpCopyUpper(char* dst, int dstSz, char* src)
{
	int i;

	i = 0;
	if (dstSz <= 0) return;
	while (src != nil && src[i] != '\0' && i < dstSz - 1) {
		char c = src[i];
		if (c >= 'a' && c <= 'z')
			c = c - 'a' + 'A';
		dst[i] = c;
		i++;
	}
	dst[i] = '\0';
}

static void
httpPathWithoutQuery(char* dst, int dstSz, char* src)
{
	int i;

	i = 0;
	if (dstSz <= 0) return;
	if (src == nil || src[0] == '\0') {
		if (dstSz > 1) {
			dst[0] = '/';
			dst[1] = '\0';
		} else {
			dst[0] = '\0';
		}
		return;
	}
	while (src[i] != '\0' && src[i] != '?' && i < dstSz - 1) {
		dst[i] = src[i];
		i++;
	}
	dst[i] = '\0';
	if (dst[0] == '\0' && dstSz > 1) {
		dst[0] = '/';
		dst[1] = '\0';
	}
}

/* Server.handle(method, path, [body], [host]) — one request, no socket. */
static Value
serverHandleNative(int argCount, Value* args)
{
	ObjInstance* server;
	ObjInstance* resObj;
	ObjInstance* out;
	char* methodSrc;
	char* pathSrc;
	char* bodySrc;
	char* hostSrc;
	char methodBuf[16];
	char pathBuf[1024];
	char hostBuf[256];
	Value statusVal, bodyVal, typeVal;
	int statusCode;

	bodySrc = "";
	hostSrc = "";
	statusCode = 200;
	if (argCount < 3 || argCount > 5 || !IS_INSTANCE(args[0]) ||
	    !IS_STRING(args[1]) || !IS_STRING(args[2]))
		return NIL_VAL;
	if (argCount >= 4) {
		if (IS_STRING(args[3])) bodySrc = AS_CSTRING(args[3]);
		else if (!IS_NIL(args[3])) return NIL_VAL;
	}
	if (argCount == 5) {
		if (IS_STRING(args[4])) hostSrc = AS_CSTRING(args[4]);
		else if (!IS_NIL(args[4])) return NIL_VAL;
	}

	server = AS_INSTANCE(args[0]);
	methodSrc = AS_CSTRING(args[1]);
	pathSrc = AS_CSTRING(args[2]);
	httpCopyUpper(methodBuf, sizeof(methodBuf), methodSrc);
	httpPathWithoutQuery(pathBuf, sizeof(pathBuf), pathSrc);
	snprint(hostBuf, sizeof(hostBuf), "%s", hostSrc);
	httpStripHostPort(hostBuf);

	resObj = newInstance(serverResClass);
	push(OBJ_VAL(resObj));
	tableSet(&resObj->fields, copyString("_statusCode", 11), NUMBER_VAL(200));
	tableSet(&resObj->fields, copyString("_body", 5), OBJ_VAL(copyString("", 0)));
	tableSet(&resObj->fields, copyString("_contentType", 12),
		OBJ_VAL(copyString("text/plain", 10)));
	serverDispatch(server, methodBuf, pathBuf, hostBuf, bodySrc, resObj);

	out = newInstance(nil);
	push(OBJ_VAL(out));
	if (tableGet(&resObj->fields, copyString("_statusCode", 11), &statusVal) && IS_NUMBER(statusVal))
		statusCode = (int)AS_NUMBER(statusVal);
	tableSet(&out->fields, copyString("statusCode", 10), NUMBER_VAL((double)statusCode));
	if (tableGet(&resObj->fields, copyString("_body", 5), &bodyVal) && IS_STRING(bodyVal))
		tableSet(&out->fields, copyString("body", 4), bodyVal);
	else
		tableSet(&out->fields, copyString("body", 4), OBJ_VAL(copyString("", 0)));
	if (tableGet(&resObj->fields, copyString("_contentType", 12), &typeVal) && IS_STRING(typeVal))
		tableSet(&out->fields, copyString("contentType", 11), typeVal);
	else
		tableSet(&out->fields, copyString("contentType", 11), OBJ_VAL(copyString("text/plain", 10)));
	pop(); /* out */
	pop(); /* res */
	return OBJ_VAL(out);
}

static void
serverAcceptLoop(ObjInstance* server, char* adir, int afd, ObjArray* routes,
	char* defaultStaticDir)
{
	USED(afd);
	for (;;) {
		char ldir[40];
		int lcfd = listen(adir, ldir);
		int dfd;
		if (lcfd < 0)
			exits("listen");
		dfd = accept(lcfd, ldir);
		close(lcfd);
		if (dfd < 0)
			continue;
		serverHandleClient(server, dfd, routes, defaultStaticDir);
		close(dfd);
	}
}

static int
serverSpawnWorker(ObjInstance* server, char* adir, int afd, ObjArray* routes,
	char* defaultStaticDir, int port)
{
	int pid;

	pid = rfork(RFPROC|RFFDG);
	if (pid < 0)
		return -1;
	if (pid == 0) {
		fprint(1, "HTTP server worker pid=%d port=%d\n", getpid(), port);
		serverAcceptLoop(server, adir, afd, routes, defaultStaticDir);
		exits(nil);
	}
	return pid;
}

/* Server.start() - blocking dispatch loop (Plan 9: announce/listen/accept) */
static Value
serverStartNative(int argCount, Value* args)
{
	ObjInstance* server;
	Value portVal;
	int port;
	int workers;
	char addr[64];
	char adir[40];
	int afd;
	Value routesVal;
	ObjArray* routes;
	Value staticVal;
	char* defaultStaticDir;
	int i;

	if (argCount != 1 || !IS_INSTANCE(args[0]))
		return BOOL_VAL(false);
	server = AS_INSTANCE(args[0]);
	if (!tableGet(&server->fields, copyString("port", 4), &portVal) || !IS_NUMBER(portVal))
		return BOOL_VAL(false);
	port = (int)AS_NUMBER(portVal);
	workers = serverGetWorkerCount(server);

	snprint(addr, sizeof(addr), "tcp!*!%d", port);
	afd = announce(addr, adir);
	if (afd < 0) {
		fprint(2, "Failed to announce on port %d\n", port);
		return BOOL_VAL(false);
	}

	fprint(1, "HTTP server listening on port %d workers=%d\n", port, workers);
	fprint(1, "Press Ctrl+C to stop\n");

	routes = nil;
	if (tableGet(&server->fields, copyString("_routes", 7), &routesVal) && IS_ARRAY(routesVal))
		routes = AS_ARRAY(routesVal);

	defaultStaticDir = nil;
	if (tableGet(&server->fields, copyString("_static_dir", 11), &staticVal) && IS_STRING(staticVal))
		defaultStaticDir = AS_CSTRING(staticVal);

	if (workers == 1) {
		serverAcceptLoop(server, adir, afd, routes, defaultStaticDir);
		close(afd);
		return BOOL_VAL(false);
	}

	for (i = 0; i < workers; i++) {
		if (serverSpawnWorker(server, adir, afd, routes, defaultStaticDir, port) < 0) {
			fprint(2, "Failed to spawn HTTP worker\n");
			close(afd);
			return BOOL_VAL(false);
		}
	}

	/* Parent: keep announce open; wait and respawn dead workers */
	for (;;) {
		Waitmsg* w = wait();
		if (w == nil)
			continue;
		free(w);
		if (serverSpawnWorker(server, adir, afd, routes, defaultStaticDir, port) < 0)
			fprint(2, "Failed to respawn HTTP worker\n");
	}
}

/* ========== AWS Signature V4 Implementation ========== */

/* Helper: hex encode bytes */
static void
hexEncode(uchar* bytes, int len, char* out)
{
	static char hex[] = "0123456789abcdef";
	for (int i = 0; i < len; i++) {
		out[i*2] = hex[bytes[i] >> 4];
		out[i*2+1] = hex[bytes[i] & 0xf];
	}
	out[len*2] = '\0';
}

/* Helper: HMAC-SHA256 */
static void
hmacSha256(uchar* key, int keyLen, uchar* data, int dataLen, uchar* out)
{
	hmac_sha2_256(data, dataLen, key, keyLen, out, nil);
}

/* Helper: SHA256 hash */
static void
sha256Hash(uchar* data, int dataLen, uchar* out)
{
	sha2_256(data, dataLen, out, nil);
}

/* Get AWS signing key */
static void
getAwsSigningKey(char* secretKey, char* dateStamp, char* region, char* service, uchar* signingKey)
{
	uchar kDate[SHA2_256dlen];
	uchar kRegion[SHA2_256dlen];
	uchar kService[SHA2_256dlen];
	
	char keyWithPrefix[256];
	snprint(keyWithPrefix, sizeof(keyWithPrefix), "AWS4%s", secretKey);
	
	hmacSha256((uchar*)keyWithPrefix, strlen(keyWithPrefix), (uchar*)dateStamp, strlen(dateStamp), kDate);
	hmacSha256(kDate, SHA2_256dlen, (uchar*)region, strlen(region), kRegion);
	hmacSha256(kRegion, SHA2_256dlen, (uchar*)service, strlen(service), kService);
	hmacSha256(kService, SHA2_256dlen, (uchar*)"aws4_request", 12, signingKey);
}

/* Create AWS Signature V4.
 * STS session tokens are often ~2KB; keep canonical buffers on the heap. */
static void
createAwsSignature(char* method, char* host, char* uri, char* queryString,
		char* payloadHash, char* accessKey, char* secretKey, char* region,
		char* service, char* amzDate, char* dateStamp, char* sessionToken,
		char* authHeader, int authHeaderLen)
{
	int tokLen = 0;
	int n, hdrCap, reqCap;
	char *canonicalHeaders, *canonicalRequest;
	char signedHeaders[256];
	uchar canonicalHash[SHA2_256dlen];
	char canonicalHashHex[SHA2_256dlen*2+1];
	char credentialScope[256];
	char stringToSign[512];
	uchar signingKey[SHA2_256dlen];
	uchar signature[SHA2_256dlen];
	char signatureHex[SHA2_256dlen*2+1];

	if(sessionToken && sessionToken[0])
		tokLen = strlen(sessionToken);
	hdrCap = 128 + strlen(host) + strlen(amzDate) + strlen(payloadHash) + tokLen + 32;
	canonicalHeaders = malloc(hdrCap);
	if(canonicalHeaders == nil){
		authHeader[0] = 0;
		return;
	}

	/* Header names must be sorted: host, x-amz-content-sha256, x-amz-date, [x-amz-security-token] */
	n = snprint(canonicalHeaders, hdrCap,
		"host:%s\nx-amz-content-sha256:%s\nx-amz-date:%s\n",
		host, payloadHash, amzDate);
	if(sessionToken && sessionToken[0] && n >= 0 && n < hdrCap){
		snprint(canonicalHeaders + n, hdrCap - n,
			"x-amz-security-token:%s\n", sessionToken);
		snprint(signedHeaders, sizeof(signedHeaders),
			"host;x-amz-content-sha256;x-amz-date;x-amz-security-token");
	}else
		snprint(signedHeaders, sizeof(signedHeaders),
			"host;x-amz-content-sha256;x-amz-date");

	reqCap = strlen(method) + strlen(uri) + strlen(queryString) +
		strlen(canonicalHeaders) + strlen(signedHeaders) + strlen(payloadHash) + 16;
	canonicalRequest = malloc(reqCap);
	if(canonicalRequest == nil){
		free(canonicalHeaders);
		authHeader[0] = 0;
		return;
	}
	snprint(canonicalRequest, reqCap,
		"%s\n%s\n%s\n%s\n%s\n%s",
		method, uri, queryString, canonicalHeaders, signedHeaders, payloadHash);

	sha256Hash((uchar*)canonicalRequest, strlen(canonicalRequest), canonicalHash);
	hexEncode(canonicalHash, SHA2_256dlen, canonicalHashHex);

	snprint(credentialScope, sizeof(credentialScope),
		"%s/%s/%s/aws4_request", dateStamp, region, service);
	snprint(stringToSign, sizeof(stringToSign),
		"AWS4-HMAC-SHA256\n%s\n%s\n%s",
		amzDate, credentialScope, canonicalHashHex);

	getAwsSigningKey(secretKey, dateStamp, region, service, signingKey);
	hmacSha256(signingKey, SHA2_256dlen, (uchar*)stringToSign, strlen(stringToSign), signature);
	hexEncode(signature, SHA2_256dlen, signatureHex);

	snprint(authHeader, authHeaderLen,
		"AWS4-HMAC-SHA256 Credential=%s/%s, SignedHeaders=%s, Signature=%s",
		accessKey, credentialScope, signedHeaders, signatureHex);

	free(canonicalRequest);
	free(canonicalHeaders);
}

/* AWS SigV4 URI-encode.
 * Query string values must encode '/' as %2F. URI paths keep '/'. */
static int
awsUriEncode(char *dst, int dstSz, char *src, int encodeSlash)
{
	int i, j;

	j = 0;
	for(i = 0; src[i] && j < dstSz-4; i++){
		char c = src[i];
		if((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		   (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~' ||
		   (c == '/' && !encodeSlash))
			dst[j++] = c;
		else{
			snprint(dst+j, 4, "%%%02X", (uchar)c);
			j += 3;
		}
	}
	dst[j] = 0;
	return j;
}

/* s3ListObjects(bucket, accessKey, secretKey, region, [prefix], [sessionToken]) -> JSON string or nil */
static Value
s3ListObjectsNative(int argCount, Value* args)
{
	if (argCount < 4 || argCount > 6)
		return NIL_VAL;
	
	if (!IS_STRING(args[0]) || !IS_STRING(args[1]) || 
	    !IS_STRING(args[2]) || !IS_STRING(args[3]))
		return NIL_VAL;
	
	char* bucket = AS_CSTRING(args[0]);
	char* accessKey = AS_CSTRING(args[1]);
	char* secretKey = AS_CSTRING(args[2]);
	char* region = AS_CSTRING(args[3]);
	char* prefix = (argCount >= 5 && IS_STRING(args[4])) ? AS_CSTRING(args[4]) : "";
	char* sessionToken = (argCount >= 6 && IS_STRING(args[5])) ? AS_CSTRING(args[5]) : nil;
	
	/* Get current time (UTC for AWS) */
	Tm* tm = gmtime(time(0));
	char amzDate[32];
	char dateStamp[16];
	snprint(amzDate, sizeof(amzDate), "%04d%02d%02dT%02d%02d%02dZ",
		tm->year+1900, tm->mon+1, tm->mday, tm->hour, tm->min, tm->sec);
	snprint(dateStamp, sizeof(dateStamp), "%04d%02d%02d",
		tm->year+1900, tm->mon+1, tm->mday);
	
	/* Build host and URI */
	char host[256];
	snprint(host, sizeof(host), "%s.s3.%s.amazonaws.com", bucket, region);
	
	char queryString[512];
	if (prefix && prefix[0]) {
		char encodedPrefix[256];
		/* Query values: '/' must be %2F or AWS's canonical request will not match. */
		awsUriEncode(encodedPrefix, sizeof(encodedPrefix), prefix, 1);
		snprint(queryString, sizeof(queryString), "list-type=2&prefix=%s", encodedPrefix);
	} else {
		snprint(queryString, sizeof(queryString), "list-type=2");
	}
	
	/* Empty payload hash (GET request) */
	char payloadHash[SHA2_256dlen*2+1];
	uchar emptyHash[SHA2_256dlen];
	sha256Hash((uchar*)"", 0, emptyHash);
	hexEncode(emptyHash, SHA2_256dlen, payloadHash);
	
	/* Create signature */
	char authHeader[512];
	createAwsSignature("GET", host, "/", queryString, payloadHash,
		accessKey, secretKey, region, "s3", amzDate, dateStamp,
		sessionToken, authHeader, sizeof(authHeader));
	
	/* Make HTTPS request */
	char dialAddr[512];
	snprint(dialAddr, sizeof(dialAddr), "tcp!%s!443", host);
	
	int fd = dial(dialAddr, nil, nil, nil);
	if (fd < 0)
		return NIL_VAL;
	
	TLSconn conn;
	memset(&conn, 0, sizeof(conn));
	conn.serverName = host;
	fd = tlsClient(fd, &conn);
	if (fd < 0)
		return NIL_VAL;
	
	/* Send request */
	char request[16384];
	int reqLen;
	if (sessionToken && sessionToken[0]) {
		reqLen = snprint(request, sizeof(request),
			"GET /?%s HTTP/1.1\r\n"
			"Host: %s\r\n"
			"Authorization: %s\r\n"
			"x-amz-date: %s\r\n"
			"x-amz-content-sha256: %s\r\n"
			"x-amz-security-token: %s\r\n"
			"Connection: close\r\n"
			"\r\n",
			queryString, host, authHeader, amzDate, payloadHash, sessionToken);
	} else {
		reqLen = snprint(request, sizeof(request),
			"GET /?%s HTTP/1.1\r\n"
			"Host: %s\r\n"
			"Authorization: %s\r\n"
			"x-amz-date: %s\r\n"
			"x-amz-content-sha256: %s\r\n"
			"Connection: close\r\n"
			"\r\n",
			queryString, host, authHeader, amzDate, payloadHash);
	}
	
	write(fd, request, reqLen);
	
	/* Read response */
	int bodyLen;
	char* respBody = readHttpResponse(fd, &bodyLen);
	
	free(conn.cert);
	close(fd);
	
	if (respBody == nil)
		return NIL_VAL;
	
	Value result = OBJ_VAL(copyString(respBody, bodyLen));
	free(respBody);
	return result;
}

/* s3GetObject(bucket, key, accessKey, secretKey, region, [sessionToken]) -> string or nil */
static Value
s3GetObjectNative(int argCount, Value* args)
{
	if (argCount < 5 || argCount > 6)
		return NIL_VAL;
	
	if (!IS_STRING(args[0]) || !IS_STRING(args[1]) || !IS_STRING(args[2]) ||
	    !IS_STRING(args[3]) || !IS_STRING(args[4]))
		return NIL_VAL;
	
	char* bucket = AS_CSTRING(args[0]);
	char* key = AS_CSTRING(args[1]);
	char* accessKey = AS_CSTRING(args[2]);
	char* secretKey = AS_CSTRING(args[3]);
	char* region = AS_CSTRING(args[4]);
	char* sessionToken = (argCount >= 6 && IS_STRING(args[5])) ? AS_CSTRING(args[5]) : nil;
	
	/* Get current time (UTC for AWS) */
	Tm* tm = gmtime(time(0));
	char amzDate[32];
	char dateStamp[16];
	snprint(amzDate, sizeof(amzDate), "%04d%02d%02dT%02d%02d%02dZ",
		tm->year+1900, tm->mon+1, tm->mday, tm->hour, tm->min, tm->sec);
	snprint(dateStamp, sizeof(dateStamp), "%04d%02d%02d",
		tm->year+1900, tm->mon+1, tm->mday);
	
	/* Build host and URI */
	char host[256];
	snprint(host, sizeof(host), "%s.s3.%s.amazonaws.com", bucket, region);
	
	/* URL encode the key */
	char uri[512] = "/";
	int uriPos = 1;
	for (int i = 0; key[i] && uriPos < sizeof(uri)-4; i++) {
		char c = key[i];
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || 
		    (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
			uri[uriPos++] = c;
		} else {
			snprint(uri+uriPos, 4, "%%%02X", (unsigned char)c);
			uriPos += 3;
		}
	}
	uri[uriPos] = '\0';
	
	/* Empty payload hash */
	char payloadHash[SHA2_256dlen*2+1];
	uchar emptyHash[SHA2_256dlen];
	sha256Hash((uchar*)"", 0, emptyHash);
	hexEncode(emptyHash, SHA2_256dlen, payloadHash);
	
	/* Create signature */
	char authHeader[512];
	createAwsSignature("GET", host, uri, "", payloadHash,
		accessKey, secretKey, region, "s3", amzDate, dateStamp,
		sessionToken, authHeader, sizeof(authHeader));
	
	/* Make HTTPS request */
	char dialAddr[512];
	snprint(dialAddr, sizeof(dialAddr), "tcp!%s!443", host);
	
	int fd = dial(dialAddr, nil, nil, nil);
	if (fd < 0)
		return NIL_VAL;
	
	TLSconn conn;
	memset(&conn, 0, sizeof(conn));
	conn.serverName = host;
	fd = tlsClient(fd, &conn);
	if (fd < 0)
		return NIL_VAL;
	
	/* Send request */
	char request[16384];
	int reqLen;
	if (sessionToken && sessionToken[0]) {
		reqLen = snprint(request, sizeof(request),
			"GET %s HTTP/1.1\r\n"
			"Host: %s\r\n"
			"Authorization: %s\r\n"
			"x-amz-date: %s\r\n"
			"x-amz-content-sha256: %s\r\n"
			"x-amz-security-token: %s\r\n"
			"Connection: close\r\n"
			"\r\n",
			uri, host, authHeader, amzDate, payloadHash, sessionToken);
	} else {
		reqLen = snprint(request, sizeof(request),
			"GET %s HTTP/1.1\r\n"
			"Host: %s\r\n"
			"Authorization: %s\r\n"
			"x-amz-date: %s\r\n"
			"x-amz-content-sha256: %s\r\n"
			"Connection: close\r\n"
			"\r\n",
			uri, host, authHeader, amzDate, payloadHash);
	}
	
	write(fd, request, reqLen);
	
	/* Read response */
	int bodyLen;
	char* respBody = readHttpResponse(fd, &bodyLen);
	
	free(conn.cert);
	close(fd);
	
	if (respBody == nil)
		return NIL_VAL;
	
	Value result = OBJ_VAL(copyString(respBody, bodyLen));
	free(respBody);
	return result;
}

/* s3PutObject(bucket, key, content, accessKey, secretKey, region, [sessionToken]) -> true/false */
static Value
s3PutObjectNative(int argCount, Value* args)
{
	if (argCount < 6 || argCount > 7)
		return BOOL_VAL(false);
	
	if (!IS_STRING(args[0]) || !IS_STRING(args[1]) || !IS_STRING(args[2]) ||
	    !IS_STRING(args[3]) || !IS_STRING(args[4]) || !IS_STRING(args[5]))
		return BOOL_VAL(false);
	
	char* bucket = AS_CSTRING(args[0]);
	char* key = AS_CSTRING(args[1]);
	char* content = AS_CSTRING(args[2]);
	int contentLen = strlen(content);
	char* accessKey = AS_CSTRING(args[3]);
	char* secretKey = AS_CSTRING(args[4]);
	char* region = AS_CSTRING(args[5]);
	char* sessionToken = (argCount >= 7 && IS_STRING(args[6])) ? AS_CSTRING(args[6]) : nil;
	
	/* Get current time (UTC for AWS) */
	Tm* tm = gmtime(time(0));
	char amzDate[32];
	char dateStamp[16];
	snprint(amzDate, sizeof(amzDate), "%04d%02d%02dT%02d%02d%02dZ",
		tm->year+1900, tm->mon+1, tm->mday, tm->hour, tm->min, tm->sec);
	snprint(dateStamp, sizeof(dateStamp), "%04d%02d%02d",
		tm->year+1900, tm->mon+1, tm->mday);
	
	/* Build host and URI */
	char host[256];
	snprint(host, sizeof(host), "%s.s3.%s.amazonaws.com", bucket, region);
	
	/* URL encode the key */
	char uri[512] = "/";
	int uriPos = 1;
	for (int i = 0; key[i] && uriPos < sizeof(uri)-4; i++) {
		char c = key[i];
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || 
		    (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
			uri[uriPos++] = c;
		} else {
			snprint(uri+uriPos, 4, "%%%02X", (unsigned char)c);
			uriPos += 3;
		}
	}
	uri[uriPos] = '\0';
	
	/* Hash the payload */
	char payloadHash[SHA2_256dlen*2+1];
	uchar contentHash[SHA2_256dlen];
	sha256Hash((uchar*)content, contentLen, contentHash);
	hexEncode(contentHash, SHA2_256dlen, payloadHash);
	
	/* Create signature */
	char authHeader[512];
	createAwsSignature("PUT", host, uri, "", payloadHash,
		accessKey, secretKey, region, "s3", amzDate, dateStamp,
		sessionToken, authHeader, sizeof(authHeader));
	
	/* Make HTTPS request */
	char dialAddr[512];
	snprint(dialAddr, sizeof(dialAddr), "tcp!%s!443", host);
	
	int fd = dial(dialAddr, nil, nil, nil);
	if (fd < 0)
		return BOOL_VAL(false);
	
	TLSconn conn;
	memset(&conn, 0, sizeof(conn));
	conn.serverName = host;
	fd = tlsClient(fd, &conn);
	if (fd < 0)
		return BOOL_VAL(false);
	
	/* Send request */
	char request[16384];
	int reqLen;
	if (sessionToken && sessionToken[0]) {
		reqLen = snprint(request, sizeof(request),
			"PUT %s HTTP/1.1\r\n"
			"Host: %s\r\n"
			"Authorization: %s\r\n"
			"x-amz-date: %s\r\n"
			"x-amz-content-sha256: %s\r\n"
			"x-amz-security-token: %s\r\n"
			"Content-Length: %d\r\n"
			"Connection: close\r\n"
			"\r\n",
			uri, host, authHeader, amzDate, payloadHash, sessionToken, contentLen);
	} else {
		reqLen = snprint(request, sizeof(request),
			"PUT %s HTTP/1.1\r\n"
			"Host: %s\r\n"
			"Authorization: %s\r\n"
			"x-amz-date: %s\r\n"
			"x-amz-content-sha256: %s\r\n"
			"Content-Length: %d\r\n"
			"Connection: close\r\n"
			"\r\n",
			uri, host, authHeader, amzDate, payloadHash, contentLen);
	}
	
	if (writeAll(fd, request, reqLen) < 0) {
		free(conn.cert);
		close(fd);
		return BOOL_VAL(false);
	}
	if (writeAll(fd, content, contentLen) < 0) {
		free(conn.cert);
		close(fd);
		return BOOL_VAL(false);
	}
	
	/* Read response */
	int bodyLen;
	char* respBody = readHttpResponse(fd, &bodyLen);
	
	free(conn.cert);
	close(fd);
	
	if (respBody == nil)
		return BOOL_VAL(false);
	
	/* Check if successful (2xx status code) */
	int success = (respBody[9] == '2');
	free(respBody);
	
	return BOOL_VAL(success);
}

/* sha256(data) -> hex string */
static Value
sha256Native(int argCount, Value* args)
{
	if (argCount != 1 || !IS_STRING(args[0]))
		return NIL_VAL;
	
	char* data = AS_CSTRING(args[0]);
	int dataLen = AS_STRING(args[0])->length;
	
	uchar hash[SHA2_256dlen];
	sha256Hash((uchar*)data, dataLen, hash);
	
	char hexHash[SHA2_256dlen*2+1];
	hexEncode(hash, SHA2_256dlen, hexHash);
	
	return OBJ_VAL(copyString(hexHash, SHA2_256dlen*2));
}

/* hmacSha256(key, data) -> hex string */
static Value
hmacSha256Native(int argCount, Value* args)
{
	if (argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1]))
		return NIL_VAL;
	
	char* key = AS_CSTRING(args[0]);
	int keyLen = AS_STRING(args[0])->length;
	char* data = AS_CSTRING(args[1]);
	int dataLen = AS_STRING(args[1])->length;
	
	uchar hmac[SHA2_256dlen];
	hmacSha256((uchar*)key, keyLen, (uchar*)data, dataLen, hmac);
	
	char hexHmac[SHA2_256dlen*2+1];
	hexEncode(hmac, SHA2_256dlen, hexHmac);
	
	return OBJ_VAL(copyString(hexHmac, SHA2_256dlen*2));
}

/* awsSignRequest(method, host, uri, queryString, payloadHash, accessKey, secretKey, region, service, amzDate, dateStamp, [sessionToken]) -> authHeader */
static Value
awsSignRequestNative(int argCount, Value* args)
{
	if (argCount < 11 || argCount > 12)
		return NIL_VAL;
	
	if (!IS_STRING(args[0]) || !IS_STRING(args[1]) || !IS_STRING(args[2]) ||
	    !IS_STRING(args[3]) || !IS_STRING(args[4]) || !IS_STRING(args[5]) ||
	    !IS_STRING(args[6]) || !IS_STRING(args[7]) || !IS_STRING(args[8]) ||
	    !IS_STRING(args[9]) || !IS_STRING(args[10]))
		return NIL_VAL;
	
	char* method = AS_CSTRING(args[0]);
	char* host = AS_CSTRING(args[1]);
	char* uri = AS_CSTRING(args[2]);
	char* queryString = AS_CSTRING(args[3]);
	char* payloadHash = AS_CSTRING(args[4]);
	char* accessKey = AS_CSTRING(args[5]);
	char* secretKey = AS_CSTRING(args[6]);
	char* region = AS_CSTRING(args[7]);
	char* service = AS_CSTRING(args[8]);
	char* amzDate = AS_CSTRING(args[9]);
	char* dateStamp = AS_CSTRING(args[10]);
	char* sessionToken = (argCount >= 12 && IS_STRING(args[11])) ? AS_CSTRING(args[11]) : nil;
	
	char authHeader[512];
	createAwsSignature(method, host, uri, queryString, payloadHash,
		accessKey, secretKey, region, service, amzDate, dateStamp,
		sessionToken, authHeader, sizeof(authHeader));
	
	return OBJ_VAL(copyString(authHeader, strlen(authHeader)));
}

/* httpServer(port, handler) -> starts server, handler gets (method, path, body) */
static Value 
httpServerNative(int argCount, Value* args)
{
	if (argCount != 1 || !IS_NUMBER(args[0]))
		return NIL_VAL;
	
	int port = (int)AS_NUMBER(args[0]);
	
	/* Announce on the specified port */
	char addr[64];
	char adir[40];
	char ldir[40];
	snprint(addr, sizeof(addr), "tcp!*!%d", port);
	
	int afd = announce(addr, adir);
	if (afd < 0) {
		fprint(2, "Failed to announce on port %d\n", port);
		return BOOL_VAL(false);
	}
	
	fprint(1, "HTTP server listening on port %d\n", port);
	fprint(1, "Press Ctrl+C to stop\n");
	
	/* Accept connections loop */
	while (1) {
		int lcfd = listen(adir, ldir);
		if (lcfd < 0) {
			close(afd);
			return BOOL_VAL(false);
		}
		
		/* Accept the connection */
		int dfd = accept(lcfd, ldir);
		close(lcfd);
		if (dfd < 0)
			continue;
		
		/* Read the request - may need multiple reads for POST body */
		char buffer[8192];
		int totalRead;
		int n;
		
		/* Read initial chunk (headers + maybe body) */
		n = read(dfd, buffer, sizeof(buffer) - 1);
		if (n <= 0) {
			close(dfd);
			continue;
		}
		totalRead = n;
		buffer[totalRead] = '\0';
		
		/* Check if we have headers complete */
		char* headerEnd = strstr(buffer, "\r\n\r\n");
		if (headerEnd == nil)
			headerEnd = strstr(buffer, "\n\n");
		
		/* If we have headers, check for Content-Length and read more if needed */
		if (headerEnd != nil) {
			char* clHeader = strstr(buffer, "Content-Length:");
			if (clHeader == nil)
				clHeader = strstr(buffer, "content-length:");
			
			if (clHeader != nil) {
				int contentLen = atoi(clHeader + 15);
				int bodyStart = (headerEnd - buffer) + 4;
				if (strstr(buffer, "\n\n") != nil && strstr(buffer, "\r\n\r\n") == nil)
					bodyStart = (headerEnd - buffer) + 2;
				
				int bodyReceived = totalRead - bodyStart;
				int needMore = contentLen - bodyReceived;
				
				/* Read remaining body if needed */
				while (needMore > 0 && totalRead < sizeof(buffer) - 1) {
					n = read(dfd, buffer + totalRead, sizeof(buffer) - totalRead - 1);
					if (n <= 0) break;
					totalRead += n;
					needMore -= n;
				}
				buffer[totalRead] = '\0';
			}
		}
		
		/* Parse request */
		HttpRequest req;
		if (parseHttpRequest(buffer, totalRead, &req) < 0) {
			sendHttpResponse(dfd, 400, "Bad Request", "{\"error\":\"Bad Request\"}");
			close(dfd);
			continue;
		}
		
		fprint(1, "%s %s\n", req.method, req.path);
		
		/* Simple routing - respond based on path */
		if (strcmp(req.path, "/") == 0) {
			sendHttpResponse(dfd, 200, "OK",
				"{\"message\":\"Hello from Lux!\",\"server\":\"lux/1.0\"}");
		} else if (strcmp(req.path, "/echo") == 0) {
			int cap = 256;
			int len = 0;
			char* responseBody = malloc(cap);
			if (responseBody == nil) {
				sendHttpResponse(dfd, 500, "Internal Server Error", "{\"error\":\"out of memory\"}");
				close(dfd);
				continue;
			}
			responseBody[0] = '\0';

			appendToBuffer(&responseBody, &len, &cap, "{\"method\":\"");
			appendJsonEscaped(&responseBody, &len, &cap, req.method);
			appendToBuffer(&responseBody, &len, &cap, "\",\"path\":\"");
			appendJsonEscaped(&responseBody, &len, &cap, req.path);
			appendToBuffer(&responseBody, &len, &cap, "\",\"body\":\"");
			appendJsonEscaped(&responseBody, &len, &cap, req.body);
			appendToBuffer(&responseBody, &len, &cap, "\"}");

			sendHttpResponse(dfd, 200, "OK", responseBody);
			free(responseBody);
		} else {
			int cap = 128;
			int len = 0;
			char* responseBody = malloc(cap);
			if (responseBody == nil) {
				sendHttpResponse(dfd, 500, "Internal Server Error", "{\"error\":\"out of memory\"}");
				close(dfd);
				continue;
			}
			responseBody[0] = '\0';

			appendToBuffer(&responseBody, &len, &cap, "{\"error\":\"Not Found\",\"path\":\"");
			appendJsonEscaped(&responseBody, &len, &cap, req.path);
			appendToBuffer(&responseBody, &len, &cap, "\"}");

			sendHttpResponse(dfd, 404, "Not Found", responseBody);
			free(responseBody);
		}
		
		close(dfd);
	}
}


static void 
resetStack(void)
{
	vm.stackTop = vm.stack;
	vm.frameCount = 0;
	vm.openUpvalues = nil;
}

static void 
runtimeError(char *format, ...)
{
	va_list args;

	va_start(args, format);
	/* Plan 9 uses fd 2 for stderr; fprint is the native print to fd */
	vfprint(2, format, args);
	va_end(args);
	fprint(2, "\n");

	/* Pointer subtraction returns long/vlong; size_t is not a Plan 9 type */

	for (int i = vm.frameCount -1; i >= 0; i--) {
		CallFrame* frame = &vm.frames[i];
		ObjFunction* function = frame->closure->function;
		long instruction = frame->ip - function->chunk.code -1;
		print("[line %d] in ", function->chunk.lines[instruction]);
		if (function->name == nil) {
			print("script\n");
		} else {
			print("%s()\n", function->name->chars);
		}
	}

	resetStack();
}

/* Native-function-safe error reporter: prints runtime-style stack trace
 * without resetting VM state, so callers can return normally.
 */
static void
nativeError(char *format, ...)
{
	char msg[1024];
	char *end;
	va_list args;

	va_start(args, format);
	end = vseprint(msg, msg + sizeof(msg), format, args);
	va_end(args);

	if (end == nil)
		print("native error\n");
	else
		print("%s\n", msg);

	for (int i = vm.frameCount -1; i >= 0; i--) {
		CallFrame* frame = &vm.frames[i];
		ObjFunction* function = frame->closure->function;
		long instruction = frame->ip - function->chunk.code -1;
		print("[line %d] in ", function->chunk.lines[instruction]);
		if (function->name == nil) {
			print("script\n");
		} else {
			print("%s()\n", function->name->chars);
		}
	}
}

static void
luxExit(int status)
{
	if (status == 0) {
		exits(nil);
	}

	char msg[32];
	snprint(msg, sizeof(msg), "exit %d", status);
	exits(msg);
}

static Value
exitNative(int argCount, Value* args)
{
	if (argCount > 1) {
		nativeError("exit() expects 0 or 1 arguments, got %d.", argCount);
		return NIL_VAL;
	}

	int status = 0;
	if (argCount == 1) {
		if (!IS_NUMBER(args[0])) {
			nativeError("exit() expects a numeric status code.");
			return NIL_VAL;
		}
		status = (int)AS_NUMBER(args[0]);
	}

	luxExit(status);
	return NIL_VAL;
}


static void 
defineNative(const char* name, NativeFn function)
{
	push(OBJ_VAL(copyString(name, (int)strlen(name))));
	push(OBJ_VAL(newNative(function)));
	tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
	pop();
	pop();
}


void 
initVM(void)
{
	resetStack();
	vm.objects = nil;

	vm.grayCount = 0;
	vm.grayCapacity = 0;
	vm.grayStack = nil;

	vm.bytesAllocated = 0;
	vm.nextGC = 1024 * 1024;
	vm.nativePanic = 0;
	vm.nativePanicMsg[0] = '\0';
	vm.scriptArgs = nil;


	initTable(&vm.globals);
	initTable(&vm.strings);
	initTable(&vm.imports);

	vm.initString = nil;
	vm.initString = copyString("init", 4);

	serverResClass = newClass(copyString("Res", 3));
	tableSet(&serverResClass->methods, copyString("send", 4), OBJ_VAL(newNative(resSendNative)));
	tableSet(&serverResClass->methods, copyString("html", 4), OBJ_VAL(newNative(resHtmlNative)));
	tableSet(&serverResClass->methods, copyString("json", 4), OBJ_VAL(newNative(resJsonNative)));
	tableSet(&serverResClass->methods, copyString("status", 6), OBJ_VAL(newNative(resStatusNative)));

	serverClass = newClass(copyString("Server", 6));
	tableSet(&serverClass->methods, vm.initString, OBJ_VAL(newNative(serverInitNative)));
	tableSet(&serverClass->methods, copyString("get", 3), OBJ_VAL(newNative(serverGetNative)));
	tableSet(&serverClass->methods, copyString("post", 4), OBJ_VAL(newNative(serverPostNative)));
	tableSet(&serverClass->methods, copyString("getHost", 7), OBJ_VAL(newNative(serverGetHostNative)));
	tableSet(&serverClass->methods, copyString("postHost", 8), OBJ_VAL(newNative(serverPostHostNative)));
	tableSet(&serverClass->methods, copyString("vhost", 5), OBJ_VAL(newNative(serverVhostNative)));
	tableSet(&serverClass->methods, copyString("use", 3), OBJ_VAL(newNative(serverUseNative)));
	tableSet(&serverClass->methods, copyString("static", 6), OBJ_VAL(newNative(serverStaticNative)));
	tableSet(&serverClass->methods, copyString("workers", 7), OBJ_VAL(newNative(serverWorkersNative)));
	tableSet(&serverClass->methods, copyString("handle", 6), OBJ_VAL(newNative(serverHandleNative)));
	tableSet(&serverClass->methods, copyString("start", 5), OBJ_VAL(newNative(serverStartNative)));
	push(OBJ_VAL(copyString("Server", 6)));
	tableSet(&vm.globals, AS_STRING(vm.stack[0]), OBJ_VAL(serverClass));
	pop();

	dictClass = newClass(copyString("Dict", 4));
	tableSet(&dictClass->methods, vm.initString, OBJ_VAL(newNative(dictInitNative)));
	tableSet(&dictClass->methods, copyString("put", 3), OBJ_VAL(newNative(dictPutNative)));
	tableSet(&dictClass->methods, copyString("get", 3), OBJ_VAL(newNative(dictGetNative)));
	tableSet(&dictClass->methods, copyString("has", 3), OBJ_VAL(newNative(dictHasNative)));
	tableSet(&dictClass->methods, copyString("remove", 6), OBJ_VAL(newNative(dictRemoveNative)));
	tableSet(&dictClass->methods, copyString("size", 4), OBJ_VAL(newNative(dictSizeNative)));
	tableSet(&dictClass->methods, copyString("clear", 5), OBJ_VAL(newNative(dictClearNative)));
	tableSet(&dictClass->methods, copyString("iter", 4), OBJ_VAL(newNative(dictIterNative)));
	push(OBJ_VAL(copyString("Dict", 4)));
	tableSet(&vm.globals, AS_STRING(vm.stack[0]), OBJ_VAL(dictClass));
	pop();

	fileClass = newClass(copyString("File", 4));
	fileSetClass(fileClass);
	tableSet(&fileClass->methods, vm.initString, OBJ_VAL(newNative(fileInitNative)));
	tableSet(&fileClass->methods, copyString("readLine", 8), OBJ_VAL(newNative(fileReadLineNative)));
	tableSet(&fileClass->methods, copyString("close", 5), OBJ_VAL(newNative(fileCloseNative)));
	push(OBJ_VAL(copyString("File", 4)));
	tableSet(&vm.globals, AS_STRING(vm.stack[0]), OBJ_VAL(fileClass));
	pop();

	ninepClass = newClass(copyString("NineP", 5));
	tableSet(&ninepClass->methods, vm.initString, OBJ_VAL(newNative(ninepInitNative)));
	tableSet(&ninepClass->methods, copyString("file", 4), OBJ_VAL(newNative(ninepFileNative)));
	tableSet(&ninepClass->methods, copyString("export", 6), OBJ_VAL(newNative(ninepExportNative)));
	tableSet(&ninepClass->methods, copyString("listen", 6), OBJ_VAL(newNative(ninepListenNative)));
	tableSet(&ninepClass->methods, copyString("post", 4), OBJ_VAL(newNative(ninepPostNative)));
	tableSet(&ninepClass->methods, copyString("connect", 7), OBJ_VAL(newNative(ninepConnectNative)));
	push(OBJ_VAL(copyString("NineP", 5)));
	tableSet(&vm.globals, AS_STRING(vm.stack[0]), OBJ_VAL(ninepClass));
	pop();

	ninepConnClass = newClass(copyString("NinePConn", 9));
	tableSet(&ninepConnClass->methods, copyString("read", 4), OBJ_VAL(newNative(ninepReadNative)));
	tableSet(&ninepConnClass->methods, copyString("write", 5), OBJ_VAL(newNative(ninepWriteNative)));
	tableSet(&ninepConnClass->methods, copyString("get", 3), OBJ_VAL(newNative(ninepGetNative)));
	tableSet(&ninepConnClass->methods, copyString("put", 3), OBJ_VAL(newNative(ninepPutNative)));
	tableSet(&ninepConnClass->methods, copyString("ls", 2), OBJ_VAL(newNative(ninepLsNative)));
	tableSet(&ninepConnClass->methods, copyString("stat", 4), OBJ_VAL(newNative(ninepStatNative)));
	tableSet(&ninepConnClass->methods, copyString("close", 5), OBJ_VAL(newNative(ninepCloseNative)));
	ninepSetClasses(ninepClass, ninepConnClass);
	push(OBJ_VAL(copyString("NinePConn", 9)));
	tableSet(&vm.globals, AS_STRING(vm.stack[0]), OBJ_VAL(ninepConnClass));
	pop();

	defineNative("assert", assertNative);
	defineNative("clock", clockNative);
	defineNative("epoch", epochNative);
	defineNative("floor", floorNative);
	defineNative("abs", absNative);
	defineNative("ceil", ceilNative);
	defineNative("sqrt", sqrtNative);
	defineNative("pow", powNative);
	defineNative("log", logNative);
	defineNative("sin", sinNative);
	defineNative("cos", cosNative);
	defineNative("help", helpNative);
	defineNative("exit", exitNative);
	defineNative("args", argsNative);
	defineNative("readFile", readFileNative);
	defineNative("renderTemplate", renderTemplateNative);
	defineNative("markdownToHtml", markdownToHtmlNative);
	defineNative("renderMarkdown", renderMarkdownNative);
	defineNative("writeFile", writeFileNative);
	defineNative("appendFile", appendFileNative);
	defineNative("deleteFile", deleteFileNative);
	defineNative("fileExists", fileExistsNative);
	defineNative("createDir", createDirNative);
	defineNative("listDir", listDirNative);
	defineNative("run", runNative);
	defineNative("netLookup", netLookupNative);
	defineNative("netPing", netPingNative);
	defineNative("len", lenNative);
	defineNative("typeof", typeofNative);
	defineNative("strFind", strFindNative);
	defineNative("strSlice", strSliceNative);
	defineNative("strStartsWithAt", strStartsWithAtNative);
	defineNative("strTrim", strTrimNative);
	defineNative("strSplit", strSplitNative);
	defineNative("arrayIndexOf", arrayIndexOfNative);
	defineNative("arrayContains", arrayContainsNative);
	defineNative("arraySort", arraySortNative);
	defineNative("arrayBinarySearch", arrayBinarySearchNative);
	registerFloat64Natives(defineNative);
	defineNative("parseJSON", parseJSONNative);
	defineNative("toJSON", toJSONNative);
	defineNative("parseCSV", parseCSVNative);
	defineNative("getField", getFieldNative);
	defineNative("parseXml", parseXmlNative);
	defineNative("httpGet", httpGetNative);
	defineNative("httpPost", httpPostNative);
	defineNative("httpPut", httpPutNative);
	defineNative("httpRequest", httpRequestNative);
	defineNative("httpPostForm", httpPostFormNative);
	defineNative("httpServer", httpServerNative);
	defineNative("sha256", sha256Native);
	defineNative("hmacSha256", hmacSha256Native);
	defineNative("awsSignRequest", awsSignRequestNative);
	defineNative("getAwsTimestamp", getAwsTimestampNative);
	defineNative("s3ListObjects", s3ListObjectsNative);
	defineNative("s3GetObject", s3GetObjectNative);
	defineNative("s3PutObject", s3PutObjectNative);
}

void
markServerRoots(void)
{
	if (serverResClass != nil) markObject((Obj*)serverResClass);
	if (serverClass != nil) markObject((Obj*)serverClass);
	if (dictClass != nil) markObject((Obj*)dictClass);
	if (fileClass != nil) markObject((Obj*)fileClass);
	if (ninepClass != nil) markObject((Obj*)ninepClass);
	if (ninepConnClass != nil) markObject((Obj*)ninepConnClass);
}

void 
freeVM(void)
{
	freeTable(&vm.globals);
	freeTable(&vm.strings);
	freeTable(&vm.imports);
	freeObjects();
}

void 
push(Value value)
{
	*vm.stackTop = value;
	vm.stackTop++;
}

Value 
pop(void)
{
	vm.stackTop--;
	return *vm.stackTop;
}

static Value 
peek(int distance)
{
	return vm.stackTop[-1 - distance];
}

static bool 
call(ObjClosure* closure, int argCount) 
{
	if (argCount != closure->function->arity) {
		runtimeError("Expected %d arguments but got %d.",
			closure->function->arity, argCount);
		return false;
	}
	if (vm.frameCount == FRAMES_MAX) {
		runtimeError("Stack overflow.");
		return false;
	}

	CallFrame* frame = &vm.frames[vm.frameCount++];
	frame->closure = closure;
	frame->ip = closure->function->chunk.code;
	frame->slots = vm.stackTop - argCount -1;
	return true;
}

int
luxInvokeClosure(ObjClosure* closure, int argCount, Value* args, Value* result)
{
	int i;
	Value* savedTop;

	if (closure == nil || result == nil)
		return 0;
	savedTop = vm.stackTop;
	push(OBJ_VAL(closure));
	for (i = 0; i < argCount; i++)
		push(args[i]);
	if (!call(closure, argCount)) {
		vm.stackTop = savedTop;
		return 0;
	}
	if (run() != INTERPRET_OK) {
		vm.stackTop = savedTop;
		return 0;
	}
	if (vm.stackTop > savedTop)
		*result = pop();
	else
		*result = NIL_VAL;
	vm.stackTop = savedTop;
	return 1;
}

static bool callValue(Value callee, int argCount) {
	if (IS_OBJ(callee)) {
		switch (OBJ_TYPE(callee)) {
		case OBJ_BOUND_METHOD: {
			ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
			vm.stackTop[-argCount -1] = bound->receiver;
			return call(bound->method, argCount);
		}
		case OBJ_CLASS: {
			ObjClass* klass = AS_CLASS(callee);
			vm.stackTop[-argCount -1] = OBJ_VAL(newInstance(klass));
			Value initializer;
			if (tableGet(&klass->methods, vm.initString, &initializer)) {
				if (IS_NATIVE(initializer)) {
					NativeFn fn = AS_NATIVE(initializer);
					fn(argCount + 1, vm.stackTop - argCount - 1);
					vm.stackTop -= argCount;
					if (vm.nativePanic) {
						vm.nativePanic = 0;
						runtimeError("%s", vm.nativePanicMsg);
						return false;
					}
					return true;
				}
				return call(AS_CLOSURE(initializer), argCount);
			} else if (argCount != 0) {
				runtimeError("Expected 0 arguments but got %d.", argCount);
				return false;
			}
			return true;
		}
			case OBJ_CLOSURE:
				return call(AS_CLOSURE(callee), argCount);
			case OBJ_NATIVE: {
			    NativeFn native;
                Value result;
                Value* args;
                native = AS_NATIVE(callee);
				/* Callee at top [arg0..argN, callee] => args = stackTop - argCount - 1;
				 * callee at bottom [callee, arg1..] => args = stackTop - argCount */
				args = (vm.stackTop[-1] == callee) ? vm.stackTop - argCount - 1 : vm.stackTop - argCount;
				result = native(argCount, args);
				vm.stackTop -= argCount + 1;
				if (vm.nativePanic) {
					vm.nativePanic = 0;
					runtimeError("%s", vm.nativePanicMsg);
					return false;
				}
				push(result);
				return true;
			}
			default:
				break; // non callable object type
		}
	}
	runtimeError("Can only call functions and classes.");
	return false;
}

static bool 
invokeFromClass(ObjClass* klass, ObjString* name, int argCount)
{
	Value method;
	if (!tableGet(&klass->methods, name, &method)) {
		runtimeError("Undefined property '%s'.", name->chars);
		return false;
	}
	if (IS_NATIVE(method)) {
		NativeFn fn = AS_NATIVE(method);
		Value result = fn(argCount + 1, vm.stackTop - argCount - 1);
		vm.stackTop -= argCount + 1;
		if (vm.nativePanic) {
			vm.nativePanic = 0;
			runtimeError("%s", vm.nativePanicMsg);
			return false;
		}
		push(result);
		return true;
	}
	return call(AS_CLOSURE(method), argCount);
}

static bool 
invoke(ObjString* name, int argCount)
{
	Value receiver = peek(argCount);

	if (!IS_INSTANCE(receiver)) {
		runtimeError("Only instances have methods.");
		return false;
	}

	ObjInstance* instance = AS_INSTANCE(receiver);

	Value value;
	if (tableGet(&instance->fields, name, &value)) {
		vm.stackTop[-argCount -1] = value;
		return callValue(value, argCount);
	}


	return invokeFromClass(instance->klass, name, argCount);
}

static bool 
bindMethod(ObjClass* klass, ObjString* name)
{
	Value method;
	if (!tableGet(&klass->methods, name, &method)) {
		runtimeError("Undefined property '%s'.", name->chars);
		return false;
	}
	
	ObjBoundMethod* bound = newBoundMethod(peek(0), AS_CLOSURE(method));
	pop();
	push(OBJ_VAL(bound));
	return true;
}


static ObjUpvalue* 
captureUpvalue(Value* local) 
{
	ObjUpvalue* prevUpvalue = nil;
	ObjUpvalue* upvalue = vm.openUpvalues;
	while (upvalue != nil && upvalue->location > local) {
		prevUpvalue = upvalue;
		upvalue = upvalue->next;
	}

	if (upvalue != nil && upvalue->location == local) {
		return upvalue;
	}

	ObjUpvalue* createdUpvalue = newUpvalue(local);
	createdUpvalue->next = upvalue;

	if (prevUpvalue == nil) {
		vm.openUpvalues = createdUpvalue;
	} else {
		prevUpvalue->next = createdUpvalue;
	}
	return createdUpvalue;
}

static void 
closeUpvalues(Value* last) 
{
	while (vm.openUpvalues != nil && vm.openUpvalues->location >= last) {
		ObjUpvalue* upvalue = vm.openUpvalues;
		upvalue->closed = *upvalue->location;
		upvalue->location = &upvalue->closed;
		vm.openUpvalues = upvalue->next;
	}
}

static void 
defineMethod(ObjString* name)
{
	Value method = peek(0);
	ObjClass* klass = AS_CLASS(peek(1));
	tableSet(&klass->methods, name, method);
	pop();
}

static int 
isFalsey(Value value)
{
	return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void
concatenate(void)
{
	Value b_val = peek(0);
	ObjString* b = AS_STRING(b_val);
	Value a_val = peek(1);
	ObjString* a = AS_STRING(a_val);
	int length = a->length + b->length;
	char* chars = ALLOCATE(char, length + 1);
	memcpy(chars, a->chars, a->length);
	memcpy(chars + a->length, b->chars, b->length);
	chars[length] = '\0';

	ObjString* result = takeString(chars, length);
	pop();
	pop();
	push(OBJ_VAL(result));
}

static bool
importModule(ObjString* path)
{
	int fd;
	long len;
	char *buf;
	Dir *d;
	long bytesRead;
	ObjFunction* function;
	ObjClosure* closure;
	Value dummy;
	
	/* Check if already imported */
	if(tableGet(&vm.imports, path, &dummy)) {
		return true;  /* Already imported, skip */
	}
	
	/* Read the file */
	fd = open(path->chars, OREAD);
	if(fd < 0) {
		runtimeError("Could not open import file '%s'.", path->chars);
		return false;
	}
	
	d = dirfstat(fd);
	if(d == nil) {
		close(fd);
		runtimeError("Could not stat import file '%s'.", path->chars);
		return false;
	}
	len = d->length;
	free(d);
	
	buf = malloc(len + 1);
	if(buf == nil) {
		close(fd);
		runtimeError("Out of memory reading import file '%s'.", path->chars);
		return false;
	}
	
	bytesRead = read(fd, buf, len);
	if(bytesRead < 0) {
		free(buf);
		close(fd);
		runtimeError("Could not read import file '%s'.", path->chars);
		return false;
	}
	
	buf[bytesRead] = '\0';
	close(fd);
	
	/* Compile the module */
	function = compile(buf);
	free(buf);
	
	if(function == nil) {
		runtimeError("Could not compile import file '%s'.", path->chars);
		return false;
	}
	
	/* Mark as imported before executing to prevent circular imports */
	tableSet(&vm.imports, path, NIL_VAL);
	
	/* Execute the module */
	push(OBJ_VAL(function));
	closure = newClosure(function);
	pop();
	push(OBJ_VAL(closure));
	if(!call(closure, 0)) {
		return false;
	}
	
	/* The module will be executed as part of the current call frame */
	/* We don't need to call run() here because we're already in run() */
	
	return true;
}

static InterpretResult 
run(void)
{
	register CallFrame* frame = &vm.frames[vm.frameCount -1];
	register uchar instruction;
	Value a, b, constant;
	double da, db;
	/* Nested run() (HTTP handlers / middleware) must stop when its
	 * callee returns, not continue into the suspended outer frames. */
	int baseFrameCount = vm.frameCount - 1;

#define READ_BYTE() (*frame->ip++)

#define READ_SHORT() \
	(frame->ip += 2, \
	(ushort)((frame->ip[-2] << 8) | frame->ip[-1]))

#define READ_CONSTANT() \
	(frame->closure->function->chunk.constants.values[READ_BYTE()])

#define READ_STRING() AS_STRING(READ_CONSTANT())
	
	for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
		/* Note: 'slot' is already declared at top of function */
		print("        ");
		for (slot = vm.stack; slot < vm.stackTop; slot++) {
			print("[ ");
			printValue(*slot);
			print(" ]");
		}
		print("\n");
		disassembleInstruction(&frame->closure->function->chunk, (int)(frame->ip - frame->closure->function->chunk.code));
#endif
		instruction = READ_BYTE();
		USED(instruction); /* Silences 'set and not used' warning */

		switch (instruction) {
		case OP_CONSTANT:
			constant = READ_CONSTANT();
			push(constant);
			break;
		
		case OP_CONSTANT_LONG: {
			uint b1, b2, b3, index;
			b1 = READ_BYTE();
			b2 = READ_BYTE();
			b3 = READ_BYTE();
			index = (b1 << 16) | (b2 << 8) | b3;
			constant = frame->closure->function->chunk.constants.values[index];
			push(constant);
			break;
		}

		case OP_NIL:   push(NIL_VAL); break;
		case OP_TRUE:  push(BOOL_VAL(1)); break;
		case OP_FALSE: push(BOOL_VAL(0)); break;
		case OP_POP: pop(); break;
		case OP_GET_LOCAL: {
			uchar slotnum;
			slotnum = READ_BYTE();
			push(frame->slots[slotnum]);
			break;
		}
		case OP_SET_LOCAL: {
			uchar slotnum;
			slotnum = READ_BYTE();
			frame->slots[slotnum] = peek(0);
			break;
		}
		case OP_GET_GLOBAL: {
			ObjString* name = READ_STRING();
			Value value;
			if (!tableGet(&vm.globals, name, &value)) {
				runtimeError("Undefined variable '%s'.", name->chars);
				return INTERPRET_RUNTIME_ERROR;
			}
			push(value);
			break;
		}
		case OP_DEFINE_GLOBAL: {
			ObjString* name = READ_STRING();
			tableSet(&vm.globals, name, peek(0));
			pop();
			break;
		}

		case OP_SET_GLOBAL: {
			ObjString* name = READ_STRING();
			if (tableSet(&vm.globals, name, peek(0))) {
				tableDelete(&vm.globals, name);
				runtimeError("Undefined variable '%s'.", name->chars);
				return INTERPRET_RUNTIME_ERROR;
			}
			break;	
		}

		case OP_GET_UPVALUE: {
			unsigned slot = READ_BYTE();
			push(*frame->closure->upvalues[slot]->location);
			break;
		}

		case OP_SET_UPVALUE: {
			unsigned slot = READ_BYTE();
			*frame->closure->upvalues[slot]->location = peek(0);
			break;
		}

		case OP_GET_PROPERTY: {
			ObjString* name = READ_STRING();
			
			/* Handle array.length */
			if (IS_ARRAY(peek(0))) {
				ObjArray* array = AS_ARRAY(peek(0));
				if (strcmp(name->chars, "length") == 0) {
					pop(); /* Array */
					push(NUMBER_VAL((double)array->count));
					break;
				} else {
					runtimeError("Arrays only have 'length' property.");
					return INTERPRET_RUNTIME_ERROR;
				}
			}

			if (IS_FLOATARRAY(peek(0))) {
				ObjFloatArray* fa = AS_FLOATARRAY(peek(0));
				if (strcmp(name->chars, "length") == 0) {
					pop();
					push(NUMBER_VAL((double)fa->length));
					break;
				} else {
					runtimeError("Float64Arrays only have 'length' property.");
					return INTERPRET_RUNTIME_ERROR;
				}
			}
			
			if (!IS_INSTANCE(peek(0))) {
				runtimeError("Only instances have properties.");
				return INTERPRET_RUNTIME_ERROR;
			}


			ObjInstance* instance = AS_INSTANCE(peek(0));

			Value value;
			if (tableGet(&instance->fields, name, &value)) {
				pop(); // Instance
				push(value);
				break;
			}

			if (!bindMethod(instance->klass, name)) {
					return INTERPRET_RUNTIME_ERROR;
				}
				break;
		}

		case OP_SET_PROPERTY: {
			if (!IS_INSTANCE(peek(1))) {
				runtimeError("Only instances have fields.");
				return INTERPRET_RUNTIME_ERROR;
			}

			ObjInstance* instance = AS_INSTANCE(peek(1));
			tableSet(&instance->fields, READ_STRING(), peek(0));
			Value value = pop();
			pop();
			push(value);
			break;
		}

		case OP_GET_SUPER: {
			ObjString* name = READ_STRING();
			ObjClass* superclass = AS_CLASS(pop());

			if (!bindMethod(superclass, name)) {
				return INTERPRET_RUNTIME_ERROR;
			}
			break;
		}

		case OP_EQUAL:
			b = pop();
			a = pop();
			push(BOOL_VAL(valuesEqual(a, b)));
			break;

		case OP_GREATER:
			b = pop(); a = pop();
			if (!IS_NUMBER(a) || !IS_NUMBER(b)) {
				runtimeError("Operands must be numbers.");
				return INTERPRET_RUNTIME_ERROR;
			}
			push(BOOL_VAL(AS_NUMBER(a) > AS_NUMBER(b)));
			break;

		case OP_LESS:
			b = pop(); a = pop();
			if (!IS_NUMBER(a) || !IS_NUMBER(b)) {
				runtimeError("Operands must be numbers (got %s < %s).",
					IS_OBJ(a) ? "obj" : (IS_NIL(a) ? "nil" : "other"),
					IS_OBJ(b) ? "obj" : (IS_NUMBER(b) ? "num" : (IS_NIL(b) ? "nil" : "other")));
				return INTERPRET_RUNTIME_ERROR;
			}
			push(BOOL_VAL(AS_NUMBER(a) < AS_NUMBER(b)));
			break;

		case OP_ADD:
			if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
				/* Both are numbers, add them numerically */
				b = pop();
				a = pop();
				push(NUMBER_VAL(AS_NUMBER(a) + AS_NUMBER(b)));
			} else if (IS_ARRAY(peek(0)) && IS_ARRAY(peek(1))) {
				/* Both are arrays, concatenate them */
				ObjArray* bArr = AS_ARRAY(pop());
				ObjArray* aArr = AS_ARRAY(pop());
				ObjArray* result = newArray();
				/* Push result first, then aArr, bArr so GC blackens sources before result */
				push(OBJ_VAL(result));
				push(OBJ_VAL(aArr));
				push(OBJ_VAL(bArr));
				for (int i = 0; i < aArr->count; i++)
					writeArray(result, aArr->elements[i]);
				for (int i = 0; i < bArr->count; i++)
					writeArray(result, bArr->elements[i]);
				/* stack: [result, aArr, bArr] -> leave result on top */
				pop(); /* bArr */
				pop(); /* aArr */
				/* result already at top */
			} else {
				/* At least one is not a number, convert both to strings and concatenate */
				b = pop();
				a = pop();
				ObjString* bStr = valueToString(b);
				ObjString* aStr = valueToString(a);

				/* Sanity check: prevent memcpy with corrupt length (e.g. array treated as string) */
				int aLen = aStr->length, bLen = bStr->length;
				if (aLen < 0 || aLen > 16*1024*1024) aLen = 0;
				if (bLen < 0 || bLen > 16*1024*1024) bLen = 0;

				int length = aLen + bLen;
				char* chars = ALLOCATE(char, length + 1);
				memcpy(chars, aStr->chars, aLen);
				memcpy(chars + aLen, bStr->chars, bLen);
				chars[length] = '\0';

				ObjString* result = takeString(chars, length);
				push(OBJ_VAL(result));
			}
			break;	

		case OP_SUBTRACT:
			b = pop(); a = pop();
			if (!IS_NUMBER(a) || !IS_NUMBER(b)) {
				runtimeError("Operands must be numbers.");
				return INTERPRET_RUNTIME_ERROR;
			}
			da = AS_NUMBER(a);
			db = AS_NUMBER(b);
			push(NUMBER_VAL(da - db));
			break;

		case OP_MULTIPLY:
			b = pop(); a = pop();
			if (!IS_NUMBER(a) || !IS_NUMBER(b)) {
				runtimeError("Operands must be numbers.");
				return INTERPRET_RUNTIME_ERROR;
			}
			da = AS_NUMBER(a);
			db = AS_NUMBER(b);
			push(NUMBER_VAL(da * db));
			break;

		case OP_DIVIDE:
			b = pop(); a = pop();
			if (!IS_NUMBER(a) || !IS_NUMBER(b)) {
				runtimeError("Operands must be numbers.");
				return INTERPRET_RUNTIME_ERROR;
			}
			da = AS_NUMBER(a);
			db = AS_NUMBER(b);
			push(NUMBER_VAL(da / db));
			break;

		case OP_MODULO:
			b = pop(); a = pop();
			if (!IS_NUMBER(a) || !IS_NUMBER(b)) {
				runtimeError("Operands must be numbers.");
				return INTERPRET_RUNTIME_ERROR;
			}
			da = AS_NUMBER(a);
			db = AS_NUMBER(b);
			/* Floating-point modulo for Plan 9 */
			push(NUMBER_VAL(da - ((long)(da / db)) * db));
			break;

		case OP_NOT:
			a = pop();
			push(BOOL_VAL(isFalsey(a)));
			break;

		case OP_NEGATE:
			if (!IS_NUMBER(peek(0))) {
				runtimeError("Operand must be a number.");
				return INTERPRET_RUNTIME_ERROR;
			}
			a = pop();
			push(NUMBER_VAL(-AS_NUMBER(a)));
			break;

		case OP_PRINT:
			a = pop();
			printValue(a);
			print("\n");
			break;
		
		case OP_IMPORT: {
			ObjString* path;
			uchar constIdx;
			constIdx = READ_BYTE();
			path = AS_STRING(frame->closure->function->chunk.constants.values[constIdx]);
			if(!importModule(path)) {
				return INTERPRET_RUNTIME_ERROR;
			}
			frame = &vm.frames[vm.frameCount -1];
			break;
		}

		case OP_JUMP: {
			ushort offset = READ_SHORT();
			frame->ip += offset;
			break;			
		}

		case OP_JUMP_IF_FALSE: {
			ushort offset = READ_SHORT();
			if (isFalsey(peek(0))) frame->ip += offset;
			break;
		}
		
		case OP_LOOP: {
			ushort offset = READ_SHORT();
			frame->ip -= offset;
			break;
		}
			
		case OP_CALL: {
			int argCount = READ_BYTE();
			if (!callValue(peek(argCount), argCount)) {
				return INTERPRET_RUNTIME_ERROR;
			}
			frame = &vm.frames[vm.frameCount -1];
			break;
		}
		case OP_INVOKE: {
			ObjString* method = READ_STRING();
			int argCount = READ_BYTE();
			if (!invoke(method, argCount)) {
				return INTERPRET_RUNTIME_ERROR;
			}
			frame = &vm.frames[vm.frameCount -1];
			break;
		}
		case OP_SUPER_INVOKE: {
			ObjString* method = READ_STRING();
			int argCount = READ_BYTE();
			ObjClass* superclass = AS_CLASS(pop());
			if (!invokeFromClass(superclass, method, argCount)) {
				return INTERPRET_RUNTIME_ERROR;
			}
			frame = &vm.frames[vm.frameCount -1];
			break;
		}

		case OP_CLOSURE: {
			ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
			ObjClosure* closure = newClosure(function);
			push(OBJ_VAL(closure));
			for (int i = 0; i < closure->upvalueCount; i++) {
				unsigned isLocal = READ_BYTE();
				unsigned index = READ_BYTE();
				if (isLocal) {
					closure->upvalues[i] = captureUpvalue(frame->slots + index);
				} else {
					closure->upvalues[i] = frame->closure->upvalues[index];
				}
			}
			break;
		}
		case OP_CLOSE_UPVALUE:
			closeUpvalues(vm.stackTop - 1);
			pop();
			break;
		case OP_RETURN: {
			Value result;
			result = pop();
			closeUpvalues(frame->slots);
			vm.frameCount--;
			if (vm.frameCount == baseFrameCount) {
				if (baseFrameCount == 0) {
					pop();
					return INTERPRET_OK;
				}
				vm.stackTop = frame->slots;
				push(result);
				return INTERPRET_OK;
			}
			
			vm.stackTop = frame->slots;
			push(result);
			frame = &vm.frames[vm.frameCount -1];
			break;
		}
		case OP_CLASS:
			push(OBJ_VAL(newClass(READ_STRING())));
			break;	
		case OP_INHERIT: {
			Value superclass = peek(1);
			if (!IS_CLASS(superclass)) {
				runtimeError("Superclasss must be a class.");
				return INTERPRET_RUNTIME_ERROR;
			}

			ObjClass* subclass = AS_CLASS(peek(0));
			tableAddAll(&AS_CLASS(superclass)->methods, &subclass->methods);
			pop(); // Subclass.
			break;
		}
		case OP_METHOD:
			defineMethod(READ_STRING());
			break;
		case OP_ARRAY: {
			int count;
			ObjArray* array;
			int i;
			
			count = READ_BYTE();
			array = newArray();
			
			/* Allocate space for all elements (might trigger GC) */
			if (count > 0) {
				/* Push array for GC protection before allocating elements */
				push(OBJ_VAL(array));
				
				array->elements = ALLOCATE(Value, count);
				array->capacity = count;
				array->count = count;
				
				/* Copy elements from stack 
				 * Stack is now: [e0, e1, ..., e(n-1), array]
				 * We want e0 at index 0, so peek(count) for e0
				 */
				for (i = 0; i < count; i++) {
					array->elements[i] = peek(count - i);
				}
				
				/* Pop array temporarily */
				pop();
			}
			
			/* Pop all element values from stack */
			for (i = 0; i < count; i++) {
				pop();
			}
			
			/* Push the array onto the stack */
			push(OBJ_VAL(array));
			break;
		}
		case OP_INDEX_SUBSCR: {
			Value index;
			Value target;
			int idx;
			ObjArray* arr;
			ObjString* str;
			
			index = pop();
			target = pop();
			
			if (!IS_NUMBER(index)) {
				runtimeError("Index must be a number.");
				return INTERPRET_RUNTIME_ERROR;
			}
			
			idx = (int)AS_NUMBER(index);

			if (IS_ARRAY(target)) {
				arr = AS_ARRAY(target);
				if (idx < 0 || idx >= arr->count) {
					runtimeError("Array index out of bounds.");
					return INTERPRET_RUNTIME_ERROR;
				}
				push(arr->elements[idx]);
			} else if (IS_STRING(target)) {
				str = AS_STRING(target);
				if (idx < 0 || idx >= str->length) {
					runtimeError("String index out of bounds.");
					return INTERPRET_RUNTIME_ERROR;
				}

				push(OBJ_VAL(copyString(str->chars + idx, 1)));
			} else {
				runtimeError("Can only index arrays or strings.");
				return INTERPRET_RUNTIME_ERROR;
			}
			break;
		}
		case OP_STORE_SUBSCR: {
			Value value;
			Value index;
			Value array;
			int idx;
			int oldCount;
			int requiredCount;
			int oldCapacity;
			int i;
			ObjArray* arr;
			
			value = pop();
			index = pop();
			array = pop();
			
			if (!IS_ARRAY(array)) {
				runtimeError("Can only index arrays.");
				return INTERPRET_RUNTIME_ERROR;
			}
			
			if (!IS_NUMBER(index)) {
				runtimeError("Array index must be a number.");
				return INTERPRET_RUNTIME_ERROR;
			}
			
			idx = (int)AS_NUMBER(index);
			arr = AS_ARRAY(array);
			
			if (idx < 0) {
				runtimeError("Array index out of bounds.");
				return INTERPRET_RUNTIME_ERROR;
			}

			if (idx >= arr->count) {
				oldCount = arr->count;
				requiredCount = idx + 1;
				oldCapacity = arr->capacity;

				while (arr->capacity < requiredCount) {
					arr->capacity = GROW_CAPACITY(arr->capacity);
				}

				if (arr->capacity != oldCapacity) {
					arr->elements = (Value*)reallocate(arr->elements,
						sizeof(Value) * oldCapacity,
						sizeof(Value) * arr->capacity);
				}

				for (i = oldCount; i < requiredCount; i++) {
					arr->elements[i] = NIL_VAL;
				}
				arr->count = requiredCount;
			}
			
			arr->elements[idx] = value;
			push(value);
			break;
		}
	  }
	}

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
}

InterpretResult 
interpret(char *source)
{
	InterpretResult result;
	ObjFunction* function = compile(source);
	if (function == nil) return INTERPRET_COMPILE_ERROR;

	push (OBJ_VAL(function));
	ObjClosure* closure = newClosure(function);
	pop();
	push(OBJ_VAL(closure));
	call(closure, 0);

	result = run();
	return result;
}
