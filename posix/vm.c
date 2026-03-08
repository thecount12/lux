#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <curl/curl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "object.h"
#include "memory.h"
#include "vm.h"

VM vm;

static Value clockNative(int argCount __attribute__((unused)), Value* args __attribute__((unused))) {
	return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

/* Native function to read a file: readFile(path) -> string */
static Value readFileNative(int argCount, Value* args) {
	if (argCount != 1 || !IS_STRING(args[0]))
		return NIL_VAL;

	char* path = AS_CSTRING(args[0]);
	FILE* file = fopen(path, "rb");
	if (file == NULL)
		return NIL_VAL;

	/* Determine file size */
	fseek(file, 0L, SEEK_END);
	size_t fileSize = ftell(file);
	rewind(file);

	char* buffer = (char*)malloc(fileSize + 1);
	if (buffer == NULL) {
		fclose(file);
		return NIL_VAL;
	}

	size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
	buffer[bytesRead] = '\0';
	fclose(file);

	/* Wrap the raw C string into a Value */
	Value result = OBJ_VAL(copyString(buffer, (int)bytesRead));
	free(buffer);
	return result;
}

/* Native function to write a file: writeFile(path, content) -> bool */
static Value writeFileNative(int argCount, Value* args) {
	if (argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1]))
		return BOOL_VAL(false);

	char* path = AS_CSTRING(args[0]);
	char* content = AS_CSTRING(args[1]);

	FILE* file = fopen(path, "wb");
	if (file == NULL)
		return BOOL_VAL(false);

	fprintf(file, "%s", content);
	fclose(file);
	return BOOL_VAL(true);
}

/* Native function to append to a file: appendFile(path, content) -> bool */
static Value appendFileNative(int argCount, Value* args) {
	if (argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1]))
		return BOOL_VAL(false);

	char* path = AS_CSTRING(args[0]);
	char* content = AS_CSTRING(args[1]);

	FILE* file = fopen(path, "ab");
	if (file == NULL)
		return BOOL_VAL(false);

	fprintf(file, "%s", content);
	fclose(file);
	return BOOL_VAL(true);
}

/* Native function to delete a file: deleteFile(path) -> bool */
static Value deleteFileNative(int argCount, Value* args) {
	if (argCount != 1 || !IS_STRING(args[0]))
		return BOOL_VAL(false);

	char* path = AS_CSTRING(args[0]);
	if (remove(path) < 0)
		return BOOL_VAL(false);
	return BOOL_VAL(true);
}

/* Native function to check if file exists: fileExists(path) -> bool */
static Value fileExistsNative(int argCount, Value* args) {
	if (argCount != 1 || !IS_STRING(args[0]))
		return BOOL_VAL(false);

	char* path = AS_CSTRING(args[0]);
	if (access(path, F_OK) == 0)
		return BOOL_VAL(true);
	return BOOL_VAL(false);
}

/* Native function to create a directory: createDir(path) -> bool */
static Value createDirNative(int argCount, Value* args) {
	if (argCount != 1 || !IS_STRING(args[0]))
		return BOOL_VAL(false);

	char* path = AS_CSTRING(args[0]);
	if (mkdir(path, 0775) < 0)
		return BOOL_VAL(false);
	return BOOL_VAL(true);
}

/* Native function to list directory contents: listDir(path) -> string */
static Value listDirNative(int argCount, Value* args) {
	if (argCount != 1 || !IS_STRING(args[0]))
		return NIL_VAL;

	char* path = AS_CSTRING(args[0]);
	DIR* dir = opendir(path);
	if (dir == NULL)
		return NIL_VAL;

	/* allocate buffer for result string */
	int resultCap = 1024;
	char* result = malloc(resultCap);
	if (result == NULL) {
		closedir(dir);
		return NIL_VAL;
	}
	int resultLen = 0;
	result[0] = '\0';

	/* read directory entries */
	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL) {
		/* skip . and .. */
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;
		
		int nameLen = strlen(entry->d_name);
		/* ensure buffer is large enough */
		while (resultLen + nameLen + 2 > resultCap) {
			resultCap *= 2;
			char* newResult = realloc(result, resultCap);
			if (newResult == NULL) {
				free(result);
				closedir(dir);
				return NIL_VAL;
			}
			result = newResult;
		}
		/* append name and newline */
		strcpy(result + resultLen, entry->d_name);
		resultLen += nameLen;
		result[resultLen++] = '\n';
		result[resultLen] = '\0';
	}

	closedir(dir);
	/* remove trailing newline if present */
	if (resultLen > 0 && result[resultLen-1] == '\n') {
		result[resultLen-1] = '\0';
		resultLen--;
	}

	Value retval = OBJ_VAL(copyString(result, resultLen));
	free(result);
	return retval;
}

static bool isAsciiWhitespace(char c) {
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static bool stringMatchAt(const char* text, int textLen, int index, const char* pattern, int patternLen) {
	if (index < 0 || index + patternLen > textLen)
		return false;

	for (int i = 0; i < patternLen; i++) {
		if (text[index + i] != pattern[i])
			return false;
	}

	return true;
}

/* strFind(haystack, needle, [start]) -> number index or -1 */
static Value strFindNative(int argCount, Value* args) {
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
static Value strStartsWithAtNative(int argCount, Value* args) {
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
static Value strSliceNative(int argCount, Value* args) {
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
static Value strTrimNative(int argCount, Value* args) {
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

/* strSplit(text, sep) -> array of strings */
static Value strSplitNative(int argCount, Value* args) {
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
	if (str == NULL) return NIL_VAL;
	
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
	double num = strtod(start, NULL);
	*end = saved;
	
	return NUMBER_VAL(num);
}

static Value parseJsonArray(JsonParser* parser) {
	parser->current++; /* skip [ */
	skipWhitespace(parser);
	
	/* Create an instance to hold array elements */
	ObjClass* arrayClass = newClass(copyString("Array", 5));
	push(OBJ_VAL(arrayClass)); /* protect from GC */
	ObjInstance* instance = newInstance(arrayClass);
	push(OBJ_VAL(instance)); /* protect from GC */
	
	int index = 0;
	
	if (*parser->current != ']') {
		for (;;) {
			skipWhitespace(parser);
			Value elem = parseJsonValue(parser);
			
			/* Set numeric key as string */
			char key[32];
			snprintf(key, sizeof(key), "%d", index);
			ObjString* keyStr = copyString(key, strlen(key));
			push(OBJ_VAL(keyStr)); /* protect from GC */
			tableSet(&instance->fields, keyStr, elem);
			pop();
			index++;
			
			skipWhitespace(parser);
			if (!matchChar(parser, ',')) break;
		}
	}
	
	skipWhitespace(parser);
	if (*parser->current == ']') parser->current++;
	
	/* Set length property */
	ObjString* lenKey = copyString("length", 6);
	push(OBJ_VAL(lenKey));
	tableSet(&instance->fields, lenKey, NUMBER_VAL(index));
	pop();
	
	pop(); /* instance */
	pop(); /* arrayClass */
	return OBJ_VAL(instance);
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
static Value parseJSONNative(int argCount, Value* args) {
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
		if (newBuf == NULL) return;
		*buffer = newBuf;
	}
	strcpy(*buffer + *len, str);
	*len += addLen;
}

static void appendChar(char** buffer, int* len, int* cap, char c) {
	if (*len + 2 > *cap) {
		*cap *= 2;
		char* newBuf = realloc(*buffer, *cap);
		if (newBuf == NULL) return;
		*buffer = newBuf;
	}
	(*buffer)[(*len)++] = c;
	(*buffer)[*len] = '\0';
}

static void serializeJsonValue(Value value, char** buffer, int* len, int* cap);

static void serializeJsonObject(ObjInstance* instance, char** buffer, int* len, int* cap) {
	appendChar(buffer, len, cap, '{');
	
	bool first = true;
	for (int i = 0; i < instance->fields.capacity; i++) {
		if (instance->fields.entries[i].key != NULL) {
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
		snprintf(numBuf, sizeof(numBuf), "%.15g", AS_NUMBER(value));
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
	} else if (IS_INSTANCE(value)) {
		serializeJsonObject(AS_INSTANCE(value), buffer, len, cap);
	}
}

/* Native function to convert to JSON: toJSON(value) -> string */
static Value toJSONNative(int argCount, Value* args) {
	if (argCount != 1)
		return NIL_VAL;
	
	int cap = 256;
	int len = 0;
	char* buffer = malloc(cap);
	if (buffer == NULL) return NIL_VAL;
	buffer[0] = '\0';
	
	serializeJsonValue(args[0], &buffer, &len, &cap);
	
	Value result = OBJ_VAL(copyString(buffer, len));
	free(buffer);
	return result;
}

/* parseXml(xmlString, tagName) -> object with numeric keys (like JSON arrays) */
static Value parseXmlNative(int argCount, Value* args) {
	if (argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1]))
		return NIL_VAL;
	
	char* xml = AS_CSTRING(args[0]);
	char* tagName = AS_CSTRING(args[1]);
	
	/* Build open and close tags */
	char openTag[256];
	char closeTag[256];
	snprintf(openTag, sizeof(openTag), "<%s>", tagName);
	snprintf(closeTag, sizeof(closeTag), "</%s>", tagName);
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
		if (openPos == NULL)
			break;
		
		/* Find closing tag after opening */
		char* closePos = strstr(openPos + openLen, closeTag);
		if (closePos == NULL)
			break;
		
		/* Extract content between tags */
		int contentLen = closePos - (openPos + openLen);
		char* content = malloc(contentLen + 1);
		if (content == NULL)
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

/* getAwsTimestamp() -> returns instance with amzDate and dateStamp fields
 * amzDate: "20240307T120000Z"
 * dateStamp: "20240307"
 */
static Value getAwsTimestampNative(int argCount, Value* args) {
	if (argCount != 0)
		return NIL_VAL;
	
	time_t now = time(NULL);
	struct tm* tm = gmtime(&now);
	
	char amzDate[32];
	char dateStamp[16];
	snprintf(amzDate, sizeof(amzDate), "%04d%02d%02dT%02d%02d%02dZ",
		tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
	snprintf(dateStamp, sizeof(dateStamp), "%04d%02d%02d",
		tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday);
	
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

/* HTTP support using libcurl */
typedef struct {
	char* data;
	size_t size;
} HttpResponse;

static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
	size_t realsize = size * nmemb;
	HttpResponse* resp = (HttpResponse*)userp;
	
	char* ptr = realloc(resp->data, resp->size + realsize + 1);
	if (ptr == NULL) {
		return 0; /* out of memory */
	}
	
	resp->data = ptr;
	memcpy(&(resp->data[resp->size]), contents, realsize);
	resp->size += realsize;
	resp->data[resp->size] = '\0';
	
	return realsize;
}

/* httpGet(url) -> string or nil */
static Value httpGetNative(int argCount, Value* args) {
	if (argCount != 1 || !IS_STRING(args[0])) {
		return NIL_VAL;
	}
	
	char* url = AS_CSTRING(args[0]);
	CURL* curl = curl_easy_init();
	if (!curl) {
		return NIL_VAL;
	}
	
	HttpResponse resp = {0};
	resp.data = malloc(1);
	resp.size = 0;
	
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&resp);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "lux/1.0");
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
	
	CURLcode res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	
	if (res != CURLE_OK) {
		free(resp.data);
		return NIL_VAL;
	}
	
	Value result = OBJ_VAL(copyString(resp.data, (int)resp.size));
	free(resp.data);
	return result;
}

/* httpPost(url, body) -> string or nil */
static Value httpPostNative(int argCount, Value* args) {
	if (argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) {
		return NIL_VAL;
	}
	
	char* url = AS_CSTRING(args[0]);
	char* body = AS_CSTRING(args[1]);
	
	CURL* curl = curl_easy_init();
	if (!curl) {
		return NIL_VAL;
	}
	
	HttpResponse resp = {0};
	resp.data = malloc(1);
	resp.size = 0;
	
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&resp);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "lux/1.0");
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
	
	/* Set Content-Type header for JSON */
	struct curl_slist* headers = NULL;
	headers = curl_slist_append(headers, "Content-Type: application/json");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	
	CURLcode res = curl_easy_perform(curl);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	
	if (res != CURLE_OK) {
		free(resp.data);
		return NIL_VAL;
	}
	
	Value result = OBJ_VAL(copyString(resp.data, (int)resp.size));
	free(resp.data);
	return result;
}

/* httpRequest(method, url, [body], [headers]) -> string or nil
 * method: "GET", "POST", "PUT", "DELETE", etc.
 * url: target URL
 * body: optional request body (nil or string)
 * headers: optional instance with header fields (nil or instance)
 */
static Value httpRequestNative(int argCount, Value* args) {
	if (argCount < 2 || argCount > 4)
		return NIL_VAL;
	
	if (!IS_STRING(args[0]) || !IS_STRING(args[1]))
		return NIL_VAL;
	
	char* method = AS_CSTRING(args[0]);
	char* url = AS_CSTRING(args[1]);
	char* body = NULL;
	ObjInstance* headersObj = NULL;
	
	/* Optional body (arg 2) */
	if (argCount >= 3 && !IS_NIL(args[2])) {
		if (!IS_STRING(args[2]))
			return NIL_VAL;
		body = AS_CSTRING(args[2]);
	}
	
	/* Optional headers (arg 3) */
	if (argCount >= 4 && !IS_NIL(args[3])) {
		if (!IS_INSTANCE(args[3]))
			return NIL_VAL;
		headersObj = AS_INSTANCE(args[3]);
	}
	
	CURL* curl = curl_easy_init();
	if (!curl)
		return NIL_VAL;
	
	HttpResponse resp = {0};
	resp.data = malloc(1);
	resp.size = 0;
	
	/* Set method */
	if (strcmp(method, "GET") == 0) {
		/* GET is default */
	} else if (strcmp(method, "POST") == 0) {
		curl_easy_setopt(curl, CURLOPT_POST, 1L);
	} else if (strcmp(method, "PUT") == 0) {
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
	} else if (strcmp(method, "DELETE") == 0) {
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
	} else if (strcmp(method, "HEAD") == 0) {
		curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
	} else {
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
	}
	
	/* Set body if provided */
	if (body != NULL) {
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
	}
	
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&resp);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "lux/1.0");
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
	
	/* Build custom headers if provided */
	struct curl_slist* headers = NULL;
	if (headersObj != NULL) {
		for (int i = 0; i < headersObj->fields.capacity; i++) {
			if (headersObj->fields.entries[i].key != NULL) {
				char* headerName = headersObj->fields.entries[i].key->chars;
				Value headerValue = headersObj->fields.entries[i].value;
				
				if (IS_STRING(headerValue)) {
					/* Convert underscores to hyphens in header names */
					char convertedName[256];
					strncpy(convertedName, headerName, sizeof(convertedName) - 1);
					convertedName[sizeof(convertedName) - 1] = '\0';
					for (char* p = convertedName; *p; p++) {
						if (*p == '_') *p = '-';
					}
					
					char headerLine[1024];
					snprintf(headerLine, sizeof(headerLine), "%s: %s",
						convertedName, AS_CSTRING(headerValue));
					headers = curl_slist_append(headers, headerLine);
				}
			}
		}
	}
	
	if (headers != NULL) {
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	}
	
	CURLcode res = curl_easy_perform(curl);
	
	if (headers != NULL)
		curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	
	if (res != CURLE_OK) {
		free(resp.data);
		return NIL_VAL;
	}
	
	Value result = OBJ_VAL(copyString(resp.data, (int)resp.size));
	free(resp.data);
	return result;
}

/* httpPut(url, body) -> string or nil */
static Value httpPutNative(int argCount, Value* args) {
	if (argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) {
		return NIL_VAL;
	}
	
	char* url = AS_CSTRING(args[0]);
	char* body = AS_CSTRING(args[1]);
	
	CURL* curl = curl_easy_init();
	if (!curl) {
		return NIL_VAL;
	}
	
	HttpResponse resp = {0};
	resp.data = malloc(1);
	resp.size = 0;
	
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&resp);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "lux/1.0");
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
	
	/* Set Content-Type header for JSON */
	struct curl_slist* headers = NULL;
	headers = curl_slist_append(headers, "Content-Type: application/json");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	
	CURLcode res = curl_easy_perform(curl);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	
	if (res != CURLE_OK) {
		free(resp.data);
		return NIL_VAL;
	}
	
	Value result = OBJ_VAL(copyString(resp.data, (int)resp.size));
	free(resp.data);
	return result;
}

/* HTTP Request parsing for server */
typedef struct {
	char method[16];
	char path[1024];
	char body[4096];
	int bodyLen;
} HttpRequest;

static int parseHttpRequest(char* buffer, int bufLen, HttpRequest* req) {
	char* p = buffer;
	
	/* Parse method (GET, POST, etc.) */
	char* methodEnd = strchr(p, ' ');
	if (methodEnd == NULL) return -1;
	int methodLen = methodEnd - p;
	if (methodLen >= 16) return -1;
	strncpy(req->method, p, methodLen);
	req->method[methodLen] = '\0';
	
	/* Parse path */
	p = methodEnd + 1;
	char* pathEnd = strchr(p, ' ');
	if (pathEnd == NULL) return -1;
	int pathLen = pathEnd - p;
	if (pathLen >= 1024) return -1;
	strncpy(req->path, p, pathLen);
	req->path[pathLen] = '\0';
	
	/* Find body (after headers) */
	char* bodyStart = strstr(buffer, "\r\n\r\n");
	if (bodyStart != NULL) {
		bodyStart += 4;
	} else {
		bodyStart = strstr(buffer, "\n\n");
		if (bodyStart != NULL)
			bodyStart += 2;
		else
			bodyStart = NULL;
	}
	
	/* Extract body if present */
	if (bodyStart != NULL) {
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

static void sendHttpResponse(int fd, int statusCode, const char* statusText, const char* body) {
	char response[8192];
	int bodyLen = strlen(body);
	
	int len = snprintf(response, sizeof(response),
		"HTTP/1.0 %d %s\r\n"
		"Content-Type: application/json\r\n"
		"Content-Length: %d\r\n"
		"Server: lux/1.0\r\n"
		"Connection: close\r\n"
		"\r\n"
		"%s",
		statusCode, statusText, bodyLen, body);
	
	write(fd, response, len);
}

/* ========== AWS Signature V4 Implementation ========== */

/* Helper: hex encode bytes */
static void hexEncode(unsigned char* bytes, int len, char* out) {
	static char hex[] = "0123456789abcdef";
	for (int i = 0; i < len; i++) {
		out[i*2] = hex[bytes[i] >> 4];
		out[i*2+1] = hex[bytes[i] & 0xf];
	}
	out[len*2] = '\0';
}

/* Helper: HMAC-SHA256 using OpenSSL */
static void hmacSha256(unsigned char* key, int keyLen, unsigned char* data, int dataLen, unsigned char* out) {
	unsigned int outLen;
	HMAC(EVP_sha256(), key, keyLen, data, dataLen, out, &outLen);
}

/* Helper: SHA256 hash */
static void sha256Hash(unsigned char* data, int dataLen, unsigned char* out) {
	SHA256(data, dataLen, out);
}

/* Get AWS signing key */
static void getAwsSigningKey(char* secretKey, char* dateStamp, char* region, char* service, unsigned char* signingKey) {
	unsigned char kDate[SHA256_DIGEST_LENGTH];
	unsigned char kRegion[SHA256_DIGEST_LENGTH];
	unsigned char kService[SHA256_DIGEST_LENGTH];
	
	char keyWithPrefix[256];
	snprintf(keyWithPrefix, sizeof(keyWithPrefix), "AWS4%s", secretKey);
	
	hmacSha256((unsigned char*)keyWithPrefix, strlen(keyWithPrefix), (unsigned char*)dateStamp, strlen(dateStamp), kDate);
	hmacSha256(kDate, SHA256_DIGEST_LENGTH, (unsigned char*)region, strlen(region), kRegion);
	hmacSha256(kRegion, SHA256_DIGEST_LENGTH, (unsigned char*)service, strlen(service), kService);
	hmacSha256(kService, SHA256_DIGEST_LENGTH, (unsigned char*)"aws4_request", 12, signingKey);
}

/* Create AWS Signature V4 */
static void createAwsSignature(char* method, char* host, char* uri, char* queryString,
		char* payloadHash, char* accessKey, char* secretKey, char* region,
		char* service, char* amzDate, char* dateStamp, char* sessionToken,
		char* authHeader, int authHeaderLen) {
	/* Canonical request */
	char canonicalHeaders[1024];
	snprintf(canonicalHeaders, sizeof(canonicalHeaders),
		"host:%s\nx-amz-date:%s\n", host, amzDate);
	
	char signedHeaders[256];
	if (sessionToken && sessionToken[0]) {
		char tokHeader[512];
		snprintf(tokHeader, sizeof(tokHeader), "x-amz-security-token:%s\n", sessionToken);
		strncat(canonicalHeaders, tokHeader, sizeof(canonicalHeaders) - strlen(canonicalHeaders) - 1);
		snprintf(signedHeaders, sizeof(signedHeaders), "host;x-amz-date;x-amz-security-token");
	} else {
		snprintf(signedHeaders, sizeof(signedHeaders), "host;x-amz-date");
	}
	
	char canonicalRequest[4096];
	snprintf(canonicalRequest, sizeof(canonicalRequest),
		"%s\n%s\n%s\n%s\n%s\n%s",
		method, uri, queryString, canonicalHeaders, signedHeaders, payloadHash);
	
	/* Hash canonical request */
	unsigned char canonicalHash[SHA256_DIGEST_LENGTH];
	sha256Hash((unsigned char*)canonicalRequest, strlen(canonicalRequest), canonicalHash);
	char canonicalHashHex[SHA256_DIGEST_LENGTH*2+1];
	hexEncode(canonicalHash, SHA256_DIGEST_LENGTH, canonicalHashHex);
	
	/* String to sign */
	char credentialScope[256];
	snprintf(credentialScope, sizeof(credentialScope),
		"%s/%s/%s/aws4_request", dateStamp, region, service);
	
	char stringToSign[4096];
	snprintf(stringToSign, sizeof(stringToSign),
		"AWS4-HMAC-SHA256\n%s\n%s\n%s",
		amzDate, credentialScope, canonicalHashHex);
	
	/* Calculate signature */
	unsigned char signingKey[SHA256_DIGEST_LENGTH];
	getAwsSigningKey(secretKey, dateStamp, region, service, signingKey);
	
	unsigned char signature[SHA256_DIGEST_LENGTH];
	hmacSha256(signingKey, SHA256_DIGEST_LENGTH, (unsigned char*)stringToSign, strlen(stringToSign), signature);
	
	char signatureHex[SHA256_DIGEST_LENGTH*2+1];
	hexEncode(signature, SHA256_DIGEST_LENGTH, signatureHex);
	
	/* Create authorization header */
	snprintf(authHeader, authHeaderLen,
		"AWS4-HMAC-SHA256 Credential=%s/%s, SignedHeaders=%s, Signature=%s",
		accessKey, credentialScope, signedHeaders, signatureHex);
}

/* Helper for S3 HTTPS requests using libcurl */
struct MemoryStruct {
	char* memory;
	size_t size;
};

static size_t WriteMemoryCallback(void* contents, size_t size, size_t nmemb, void* userp) {
	size_t realsize = size * nmemb;
	struct MemoryStruct* mem = (struct MemoryStruct*)userp;
	
	char* ptr = realloc(mem->memory, mem->size + realsize + 1);
	if (ptr == NULL) {
		fprintf(stderr, "Not enough memory\n");
		return 0;
	}
	
	mem->memory = ptr;
	memcpy(&(mem->memory[mem->size]), contents, realsize);
	mem->size += realsize;
	mem->memory[mem->size] = 0;
	
	return realsize;
}

/* s3ListObjects(bucket, accessKey, secretKey, region, [prefix], [sessionToken]) -> JSON string or nil */
static Value s3ListObjectsNative(int argCount, Value* args) {
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
	char* sessionToken = (argCount >= 6 && IS_STRING(args[5])) ? AS_CSTRING(args[5]) : NULL;
	
	/* Get current time in UTC */
	time_t now = time(NULL);
	struct tm* tm = gmtime(&now);
	char amzDate[32];
	char dateStamp[16];
	snprintf(amzDate, sizeof(amzDate), "%04d%02d%02dT%02d%02d%02dZ",
		tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
	snprintf(dateStamp, sizeof(dateStamp), "%04d%02d%02d",
		tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday);
	
	/* Build host and URL */
	char host[256];
	snprintf(host, sizeof(host), "%s.s3.%s.amazonaws.com", bucket, region);
	
	/* URL encode prefix using libcurl */
	char* encodedPrefix = NULL;
	if (prefix && prefix[0]) {
		encodedPrefix = curl_easy_escape(NULL, prefix, strlen(prefix));
	}
	
	char queryString[512];
	if (encodedPrefix) {
		snprintf(queryString, sizeof(queryString), "list-type=2&prefix=%s", encodedPrefix);
		curl_free(encodedPrefix);
	} else {
		snprintf(queryString, sizeof(queryString), "list-type=2");
	}
	
	/* Empty payload hash */
	char payloadHash[SHA256_DIGEST_LENGTH*2+1];
	unsigned char emptyHash[SHA256_DIGEST_LENGTH];
	sha256Hash((unsigned char*)"", 0, emptyHash);
	hexEncode(emptyHash, SHA256_DIGEST_LENGTH, payloadHash);
	
	/* Create signature */
	char authHeader[512];
	createAwsSignature("GET", host, "/", queryString, payloadHash,
		accessKey, secretKey, region, "s3", amzDate, dateStamp,
		sessionToken, authHeader, sizeof(authHeader));
	
	/* Build full URL */
	char url[1024];
	snprintf(url, sizeof(url), "https://%s/?%s", host, queryString);
	
	/* Setup libcurl */
	CURL* curl = curl_easy_init();
	if (!curl)
		return NIL_VAL;
	
	struct MemoryStruct chunk;
	chunk.memory = malloc(1);
	chunk.size = 0;
	
	/* Set headers */
	struct curl_slist* headers = NULL;
	char authHdr[1024];
	snprintf(authHdr, sizeof(authHdr), "Authorization: %s", authHeader);
	headers = curl_slist_append(headers, authHdr);
	
	char dateHdr[128];
	snprintf(dateHdr, sizeof(dateHdr), "x-amz-date: %s", amzDate);
	headers = curl_slist_append(headers, dateHdr);
	
	char shaHdr[256];
	snprintf(shaHdr, sizeof(shaHdr), "x-amz-content-sha256: %s", payloadHash);
	headers = curl_slist_append(headers, shaHdr);
	
	if (sessionToken && sessionToken[0]) {
		char tokenHdr[1024];
		snprintf(tokenHdr, sizeof(tokenHdr), "x-amz-security-token: %s", sessionToken);
		headers = curl_slist_append(headers, tokenHdr);
	}
	
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
	
	/* Perform request */
	CURLcode res = curl_easy_perform(curl);
	
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	
	if (res != CURLE_OK) {
		free(chunk.memory);
		return NIL_VAL;
	}
	
	Value result = OBJ_VAL(copyString(chunk.memory, chunk.size));
	free(chunk.memory);
	return result;
}

/* s3GetObject(bucket, key, accessKey, secretKey, region, [sessionToken]) -> string or nil */
static Value s3GetObjectNative(int argCount, Value* args) {
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
	char* sessionToken = (argCount >= 6 && IS_STRING(args[5])) ? AS_CSTRING(args[5]) : NULL;
	
	/* Get current time in UTC */
	time_t now = time(NULL);
	struct tm* tm = gmtime(&now);
	char amzDate[32];
	char dateStamp[16];
	snprintf(amzDate, sizeof(amzDate), "%04d%02d%02dT%02d%02d%02dZ",
		tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
	snprintf(dateStamp, sizeof(dateStamp), "%04d%02d%02d",
		tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday);
	
	/* Build host and URI */
	char host[256];
	snprintf(host, sizeof(host), "%s.s3.%s.amazonaws.com", bucket, region);
	
	/* URL encode the key */
	char* encodedKey = curl_easy_escape(NULL, key, strlen(key));
	if (!encodedKey)
		return NIL_VAL;
	
	char uri[512];
	snprintf(uri, sizeof(uri), "/%s", encodedKey);
	
	/* Empty payload hash */
	char payloadHash[SHA256_DIGEST_LENGTH*2+1];
	unsigned char emptyHash[SHA256_DIGEST_LENGTH];
	sha256Hash((unsigned char*)"", 0, emptyHash);
	hexEncode(emptyHash, SHA256_DIGEST_LENGTH, payloadHash);
	
	/* Create signature */
	char authHeader[512];
	createAwsSignature("GET", host, uri, "", payloadHash,
		accessKey, secretKey, region, "s3", amzDate, dateStamp,
		sessionToken, authHeader, sizeof(authHeader));
	
	/* Build full URL */
	char url[1024];
	snprintf(url, sizeof(url), "https://%s%s", host, uri);
	curl_free(encodedKey);
	
	/* Setup libcurl */
	CURL* curl = curl_easy_init();
	if (!curl)
		return NIL_VAL;
	
	struct MemoryStruct chunk;
	chunk.memory = malloc(1);
	chunk.size = 0;
	
	/* Set headers */
	struct curl_slist* headers = NULL;
	char authHdr[1024];
	snprintf(authHdr, sizeof(authHdr), "Authorization: %s", authHeader);
	headers = curl_slist_append(headers, authHdr);
	
	char dateHdr[128];
	snprintf(dateHdr, sizeof(dateHdr), "x-amz-date: %s", amzDate);
	headers = curl_slist_append(headers, dateHdr);
	
	char shaHdr[256];
	snprintf(shaHdr, sizeof(shaHdr), "x-amz-content-sha256: %s", payloadHash);
	headers = curl_slist_append(headers, shaHdr);
	
	if (sessionToken && sessionToken[0]) {
		char tokenHdr[1024];
		snprintf(tokenHdr, sizeof(tokenHdr), "x-amz-security-token: %s", sessionToken);
		headers = curl_slist_append(headers, tokenHdr);
	}
	
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
	
	/* Perform request */
	CURLcode res = curl_easy_perform(curl);
	
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	
	if (res != CURLE_OK) {
		free(chunk.memory);
		return NIL_VAL;
	}
	
	Value result = OBJ_VAL(copyString(chunk.memory, chunk.size));
	free(chunk.memory);
	return result;
}

/* s3PutObject(bucket, key, content, accessKey, secretKey, region, [sessionToken]) -> true/false */
static Value s3PutObjectNative(int argCount, Value* args) {
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
	char* sessionToken = (argCount >= 7 && IS_STRING(args[6])) ? AS_CSTRING(args[6]) : NULL;
	
	/* Get current time in UTC */
	time_t now = time(NULL);
	struct tm* tm = gmtime(&now);
	char amzDate[32];
	char dateStamp[16];
	snprintf(amzDate, sizeof(amzDate), "%04d%02d%02dT%02d%02d%02dZ",
		tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
	snprintf(dateStamp, sizeof(dateStamp), "%04d%02d%02d",
		tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday);
	
	/* Build host and URI */
	char host[256];
	snprintf(host, sizeof(host), "%s.s3.%s.amazonaws.com", bucket, region);
	
	/* URL encode the key */
	char* encodedKey = curl_easy_escape(NULL, key, strlen(key));
	if (!encodedKey)
		return BOOL_VAL(false);
	
	char uri[512];
	snprintf(uri, sizeof(uri), "/%s", encodedKey);
	
	/* Hash the payload */
	char payloadHash[SHA256_DIGEST_LENGTH*2+1];
	unsigned char contentHash[SHA256_DIGEST_LENGTH];
	sha256Hash((unsigned char*)content, contentLen, contentHash);
	hexEncode(contentHash, SHA256_DIGEST_LENGTH, payloadHash);
	
	/* Create signature */
	char authHeader[512];
	createAwsSignature("PUT", host, uri, "", payloadHash,
		accessKey, secretKey, region, "s3", amzDate, dateStamp,
		sessionToken, authHeader, sizeof(authHeader));
	
	/* Build full URL */
	char url[1024];
	snprintf(url, sizeof(url), "https://%s%s", host, uri);
	curl_free(encodedKey);
	
	/* Setup libcurl */
	CURL* curl = curl_easy_init();
	if (!curl)
		return BOOL_VAL(false);
	
	struct MemoryStruct chunk;
	chunk.memory = malloc(1);
	chunk.size = 0;
	
	/* Set headers */
	struct curl_slist* headers = NULL;
	char authHdr[1024];
	snprintf(authHdr, sizeof(authHdr), "Authorization: %s", authHeader);
	headers = curl_slist_append(headers, authHdr);
	
	char dateHdr[128];
	snprintf(dateHdr, sizeof(dateHdr), "x-amz-date: %s", amzDate);
	headers = curl_slist_append(headers, dateHdr);
	
	char shaHdr[256];
	snprintf(shaHdr, sizeof(shaHdr), "x-amz-content-sha256: %s", payloadHash);
	headers = curl_slist_append(headers, shaHdr);
	
	if (sessionToken && sessionToken[0]) {
		char tokenHdr[1024];
		snprintf(tokenHdr, sizeof(tokenHdr), "x-amz-security-token: %s", sessionToken);
		headers = curl_slist_append(headers, tokenHdr);
	}
	
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
	curl_easy_setopt(curl, CURLOPT_READDATA, NULL);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, content);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)contentLen);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
	
	/* Perform request */
	CURLcode res = curl_easy_perform(curl);
	
	long response_code;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
	
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	free(chunk.memory);
	
	if (res != CURLE_OK)
		return BOOL_VAL(false);
	
	return BOOL_VAL(response_code >= 200 && response_code < 300);
}

/* sha256(data) -> hex string */
static Value sha256Native(int argCount, Value* args) {
	if (argCount != 1 || !IS_STRING(args[0]))
		return NIL_VAL;
	
	char* data = AS_CSTRING(args[0]);
	int dataLen = AS_STRING(args[0])->length;
	
	unsigned char hash[SHA256_DIGEST_LENGTH];
	sha256Hash((unsigned char*)data, dataLen, hash);
	
	char hexHash[SHA256_DIGEST_LENGTH*2+1];
	hexEncode(hash, SHA256_DIGEST_LENGTH, hexHash);
	
	return OBJ_VAL(copyString(hexHash, SHA256_DIGEST_LENGTH*2));
}

/* hmacSha256(key, data) -> hex string */
static Value hmacSha256Native(int argCount, Value* args) {
	if (argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1]))
		return NIL_VAL;
	
	char* key = AS_CSTRING(args[0]);
	int keyLen = AS_STRING(args[0])->length;
	char* data = AS_CSTRING(args[1]);
	int dataLen = AS_STRING(args[1])->length;
	
	unsigned char hmac[SHA256_DIGEST_LENGTH];
	hmacSha256((unsigned char*)key, keyLen, (unsigned char*)data, dataLen, hmac);
	
	char hexHmac[SHA256_DIGEST_LENGTH*2+1];
	hexEncode(hmac, SHA256_DIGEST_LENGTH, hexHmac);
	
	return OBJ_VAL(copyString(hexHmac, SHA256_DIGEST_LENGTH*2));
}

/* awsSignRequest(method, host, uri, queryString, payloadHash, accessKey, secretKey, region, service, amzDate, dateStamp, [sessionToken]) -> authHeader */
static Value awsSignRequestNative(int argCount, Value* args) {
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
	char* sessionToken = (argCount >= 12 && IS_STRING(args[11])) ? AS_CSTRING(args[11]) : NULL;
	
	char authHeader[512];
	createAwsSignature(method, host, uri, queryString, payloadHash,
		accessKey, secretKey, region, service, amzDate, dateStamp,
		sessionToken, authHeader, sizeof(authHeader));
	
	return OBJ_VAL(copyString(authHeader, strlen(authHeader)));
}

/* httpServer(port) -> starts server */
static Value httpServerNative(int argCount, Value* args) {
	if (argCount != 1 || !IS_NUMBER(args[0]))
		return NIL_VAL;
	
	int port = (int)AS_NUMBER(args[0]);
	
	/* Create socket */
	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd < 0) {
		fprintf(stderr, "Failed to create socket\n");
		return BOOL_VAL(false);
	}
	
	/* Allow address reuse */
	int opt = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
		close(server_fd);
		return BOOL_VAL(false);
	}
	
	/* Bind to port */
	struct sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(port);
	
	if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
		fprintf(stderr, "Failed to bind to port %d\n", port);
		close(server_fd);
		return BOOL_VAL(false);
	}
	
	/* Listen for connections */
	if (listen(server_fd, 10) < 0) {
		close(server_fd);
		return BOOL_VAL(false);
	}
	
	fprintf(stdout, "HTTP server listening on port %d\n", port);
	fprintf(stdout, "Press Ctrl+C to stop\n");
	fflush(stdout);
	
	/* Accept connections loop */
	while (1) {
		struct sockaddr_in client_addr;
		socklen_t client_len = sizeof(client_addr);
		int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
		
		if (client_fd < 0)
			continue;
		
		/* Read the request - may need multiple reads for POST body */
		char buffer[8192];
		int totalRead = 0;
		int n;
		
		/* Read initial chunk (headers + maybe body) */
		n = read(client_fd, buffer, sizeof(buffer) - 1);
		if (n <= 0) {
			close(client_fd);
			continue;
		}
		totalRead = n;
		buffer[totalRead] = '\0';
		
		/* Check if we have headers complete */
		char* headerEnd = strstr(buffer, "\r\n\r\n");
		if (headerEnd == NULL)
			headerEnd = strstr(buffer, "\n\n");
		
		/* If we have headers, check for Content-Length and read more if needed */
		if (headerEnd != NULL) {
			char* clHeader = strstr(buffer, "Content-Length:");
			if (clHeader == NULL)
				clHeader = strstr(buffer, "content-length:");
			
			if (clHeader != NULL) {
				int contentLen = atoi(clHeader + 15);
				int bodyStart = (headerEnd - buffer) + 4;
				if (strstr(buffer, "\n\n") != NULL && strstr(buffer, "\r\n\r\n") == NULL)
					bodyStart = (headerEnd - buffer) + 2;
				
				int bodyReceived = totalRead - bodyStart;
				int needMore = contentLen - bodyReceived;
				
				/* Read remaining body if needed */
				while (needMore > 0 && totalRead < (int)sizeof(buffer) - 1) {
					n = read(client_fd, buffer + totalRead, sizeof(buffer) - totalRead - 1);
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
			sendHttpResponse(client_fd, 400, "Bad Request", "{\"error\":\"Bad Request\"}");
			close(client_fd);
			continue;
		}
		
		fprintf(stdout, "%s %s\n", req.method, req.path);
		fflush(stdout);
		
		/* Simple routing - respond based on path */
		char responseBody[4096];
		if (strcmp(req.path, "/") == 0) {
			snprintf(responseBody, sizeof(responseBody),
				"{\"message\":\"Hello from Lux!\",\"server\":\"lux/1.0\"}");
			sendHttpResponse(client_fd, 200, "OK", responseBody);
		} else if (strcmp(req.path, "/echo") == 0) {
			snprintf(responseBody, sizeof(responseBody),
				"{\"method\":\"%s\",\"path\":\"%s\",\"body\":\"%s\"}",
				req.method, req.path, req.body);
			sendHttpResponse(client_fd, 200, "OK", responseBody);
		} else {
			snprintf(responseBody, sizeof(responseBody),
				"{\"error\":\"Not Found\",\"path\":\"%s\"}", req.path);
			sendHttpResponse(client_fd, 404, "Not Found", responseBody);
		}
		
		close(client_fd);
	}
	
	close(server_fd);
	return BOOL_VAL(true);
}

static void resetStack() {
	vm.stackTop = vm.stack;
	vm.frameCount = 0;
	vm.openUpvalues = NULL;
}

static void runtimeError(const char* format, ...) {
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputs("\n", stderr);

	for (int i = vm.frameCount -1; i >= 0; i--) {
		CallFrame* frame = &vm.frames[i];
		ObjFunction* function = frame->closure->function;
		size_t instruction = frame->ip - function->chunk.code -1;
		fprintf(stderr, "[line %d] in ", 
			function->chunk.lines[instruction]);
		if (function->name == NULL) {
			fprintf(stderr, "script\n");
		} else {
			fprintf(stderr, "%s()\n", function->name->chars);
		}
	}

	resetStack();
}

static void defineNative(const char* name, NativeFn function) {
	push(OBJ_VAL(copyString(name, (int)strlen(name))));
	push(OBJ_VAL(newNative(function)));
	tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
	pop();
	pop();
}

void initVM() {
	resetStack();
	vm.objects = NULL;

	vm.grayCount = 0;
	vm.grayCapacity = 0;
	vm.grayStack = NULL;

	vm.bytesAllocated = 0;
	vm.nextGC = 1024 * 1024;

	initTable(&vm.globals);
	initTable(&vm.strings);
	initTable(&vm.imports);

	vm.initString = NULL;
	vm.initString = copyString("init", 4);

	defineNative("clock", clockNative);
	defineNative("readFile", readFileNative);
	defineNative("writeFile", writeFileNative);
	defineNative("appendFile", appendFileNative);
	defineNative("deleteFile", deleteFileNative);
	defineNative("fileExists", fileExistsNative);
	defineNative("createDir", createDirNative);
	defineNative("listDir", listDirNative);
	defineNative("strFind", strFindNative);
	defineNative("strSlice", strSliceNative);
	defineNative("strStartsWithAt", strStartsWithAtNative);
	defineNative("strTrim", strTrimNative);
	defineNative("strSplit", strSplitNative);
	defineNative("parseJSON", parseJSONNative);
	defineNative("toJSON", toJSONNative);
	defineNative("parseXml", parseXmlNative);
	defineNative("httpGet", httpGetNative);
	defineNative("httpPost", httpPostNative);
	defineNative("httpPut", httpPutNative);
	defineNative("httpRequest", httpRequestNative);
	defineNative("httpServer", httpServerNative);
	defineNative("sha256", sha256Native);
	defineNative("hmacSha256", hmacSha256Native);
	defineNative("awsSignRequest", awsSignRequestNative);
	defineNative("getAwsTimestamp", getAwsTimestampNative);
	defineNative("s3ListObjects", s3ListObjectsNative);
	defineNative("s3GetObject", s3GetObjectNative);
	defineNative("s3PutObject", s3PutObjectNative);
}

void freeVM() {
	freeTable(&vm.globals);
	freeTable(&vm.strings);
	freeTable(&vm.imports);
	freeObjects();
}

void push(Value value) {
	*vm.stackTop = value;
	vm.stackTop++;
}

Value pop() {
	vm.stackTop--;
	return *vm.stackTop;
}

static Value peek(int distance) {
	return vm.stackTop[-1 - distance];
}

static bool call(ObjClosure* closure, int argCount) {
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
					return call(AS_CLOSURE(initializer), argCount);
				} else if (argCount != 0 ) {
					runtimeError("Expected 0 arguments but got %d.", argCount);
					return false;
				}
				return true;
			}
			case OBJ_CLOSURE:
				return call(AS_CLOSURE(callee), argCount);
			case OBJ_NATIVE: {
				NativeFn native = AS_NATIVE(callee);	
				Value result = native(argCount, vm.stackTop - argCount);
				vm.stackTop -= argCount + 1;
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

static bool invokeFromClass(ObjClass* klass, ObjString* name, int argCount) {
	Value method;
	if (!tableGet(&klass->methods, name, &method)) {
		runtimeError("Undefined property '%s'.", name->chars);
		return false;
	}
	return call(AS_CLOSURE(method), argCount);
}

static bool invoke(ObjString* name, int argCount) {
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

static bool bindMethod(ObjClass* klass, ObjString* name) {
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

static ObjUpvalue* captureUpvalue(Value* local) {
	ObjUpvalue* prevUpvalue = NULL;
	ObjUpvalue* upvalue = vm.openUpvalues;
	while (upvalue != NULL && upvalue->location > local) {
		prevUpvalue = upvalue;
		upvalue = upvalue->next;
	}

	if (upvalue != NULL && upvalue->location == local) {
		return upvalue;
	}

	ObjUpvalue* createdUpvalue = newUpvalue(local);
	createdUpvalue->next = upvalue;

	if (prevUpvalue == NULL) {
		vm.openUpvalues = createdUpvalue;
	} else {
		prevUpvalue->next = createdUpvalue;
	}
	return createdUpvalue;
}

static void closeUpvalues(Value* last) {
	while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
		ObjUpvalue* upvalue = vm.openUpvalues;
		upvalue->closed = *upvalue->location;
		upvalue->location = &upvalue->closed;
		vm.openUpvalues = upvalue->next;
	}
}

static void defineMethod(ObjString* name) {
	Value method = peek(0);
	ObjClass* klass = AS_CLASS(peek(1));
	tableSet(&klass->methods, name, method);
	pop();
}

static bool isFalsey(Value value) {
	return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate() {
	ObjString* b = AS_STRING(peek(0));
	ObjString* a = AS_STRING(peek(1));

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

static bool importModule(ObjString* path) {
	Value dummy;
	
	/* Check if already imported */
	if (tableGet(&vm.imports, path, &dummy)) {
		return true;  /* Already imported, skip */
	}
	
	/* Read the file */
	FILE* file = fopen(path->chars, "rb");
	if (file == NULL) {
		runtimeError("Could not open import file '%s'.", path->chars);
		return false;
	}
	
	fseek(file, 0L, SEEK_END);
	size_t fileSize = ftell(file);
	rewind(file);
	
	char* buffer = (char*)malloc(fileSize + 1);
	if (buffer == NULL) {
		fclose(file);
		runtimeError("Out of memory reading import file '%s'.", path->chars);
		return false;
	}
	
	size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
	if (bytesRead < fileSize) {
		free(buffer);
		fclose(file);
		runtimeError("Could not read import file '%s'.", path->chars);
		return false;
	}
	
	buffer[bytesRead] = '\0';
	fclose(file);
	
	/* Compile the module */
	ObjFunction* function = compile(buffer);
	free(buffer);
	
	if (function == NULL) {
		runtimeError("Could not compile import file '%s'.", path->chars);
		return false;
	}
	
	/* Mark as imported before executing to prevent circular imports */
	tableSet(&vm.imports, path, NIL_VAL);
	
	/* Execute the module */
	push(OBJ_VAL(function));
	ObjClosure* closure = newClosure(function);
	pop();
	push(OBJ_VAL(closure));
	if (!call(closure, 0)) {
		return false;
	}
	
	return true;
}

static InterpretResult run() {
	CallFrame* frame = &vm.frames[vm.frameCount -1];

#define READ_BYTE() (*frame->ip++)

#define READ_SHORT() \
	(frame->ip += 2, \
	(uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))

#define READ_CONSTANT() \
	(frame->closure->function->chunk.constants.values[READ_BYTE()])

#define READ_STRING() AS_STRING(READ_CONSTANT())
#define BINARY_OP(valueType, op) \
	do { \
		if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
			runtimeError("Operands must be numbers."); \
			return INTERPRET_RUNTIME_ERROR; \
		} \
		double b = AS_NUMBER(pop()); \
		double a = AS_NUMBER(pop()); \
		push(valueType(a op b)); \
	} while (false)
// note: while(false) plan9 C does not use false

	for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
	printf("        ");
	for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
		printf("[ ");
		printValue(*slot);
		printf(" ]");
	}
	printf("\n");
	disassembleInstruction(&frame->closure->function->chunk, (int)(frame->ip - frame->closure->function->chunk.code));
#endif
		uint8_t instruction;
		switch (instruction = READ_BYTE()) {
			case OP_CONSTANT: {
				Value constant = READ_CONSTANT();
				push(constant);
				break;
			}
			case OP_CONSTANT_LONG: {
				uint32_t b1 = READ_BYTE();
				uint32_t b2 = READ_BYTE();
				uint32_t b3 = READ_BYTE();
				uint32_t index = (b1 << 16) | (b2 << 8) | b3;
				Value constant = frame->closure->function->chunk.constants.values[index];
				push(constant);
				break;
			}
			case OP_NIL: push(NIL_VAL); break;
			case OP_TRUE: push(BOOL_VAL(true)); break;
			case OP_FALSE: push(BOOL_VAL(false)); break;
			case OP_POP: pop(); break;
			case OP_GET_LOCAL: {
				uint8_t slot = READ_BYTE();
				push(frame->slots[slot]);
				break;
			}
			case OP_SET_LOCAL: {
				uint8_t slot = READ_BYTE();
				frame->slots[slot] = peek(0);
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
					runtimeError("Undefined variabale '%s'.", name->chars);
					return INTERPRET_RUNTIME_ERROR;
				}
				break;
			}
			case OP_GET_UPVALUE: {
				uint8_t slot = READ_BYTE();
				push(*frame->closure->upvalues[slot]->location);
				break;
			}
			case OP_SET_UPVALUE: {
				uint8_t slot = READ_BYTE();
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

			case OP_EQUAL: {
				Value b = pop();
				Value a = pop();
				push(BOOL_VAL(valuesEqual(a, b)));
				break;
			}
			case OP_GREATER: 	BINARY_OP(BOOL_VAL, >); break;
			case OP_LESS: 		BINARY_OP(BOOL_VAL, <); break;
			case OP_ADD: {
				if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
					/* Both are numbers, add them numerically */
					double b = AS_NUMBER(pop());
					double a = AS_NUMBER(pop());
					push(NUMBER_VAL(a + b));
				} else {
					/* At least one is not a number, convert both to strings and concatenate */
					Value b = pop();
					Value a = pop();
					ObjString* bStr = valueToString(b);
					ObjString* aStr = valueToString(a);
					
					int length = aStr->length + bStr->length;
					char* chars = ALLOCATE(char, length + 1);
					memcpy(chars, aStr->chars, aStr->length);
					memcpy(chars + aStr->length, bStr->chars, bStr->length);
					chars[length] = '\0';
					
					ObjString* result = takeString(chars, length);
					push(OBJ_VAL(result));
				}
				break;
			}	
			case OP_SUBTRACT:	BINARY_OP(NUMBER_VAL, -); break;
			case OP_MULTIPLY:	BINARY_OP(NUMBER_VAL, *); break;
			case OP_DIVIDE:		BINARY_OP(NUMBER_VAL, /); break;
			case OP_NOT:
				push(BOOL_VAL(isFalsey(pop())));
				break;
			case OP_NEGATE:
				if (!IS_NUMBER(peek(0))) {
					runtimeError("Operand must be a number.");
					return INTERPRET_RUNTIME_ERROR;
				}
				push(NUMBER_VAL(-AS_NUMBER(pop())));
				break;
			case OP_PRINT: {
				printValue(pop());
				printf("\n");
				break;
			}
			case OP_IMPORT: {
				uint8_t constIdx = READ_BYTE();
				ObjString* path = AS_STRING(frame->closure->function->chunk.constants.values[constIdx]);
				if (!importModule(path)) {
					return INTERPRET_RUNTIME_ERROR;
				}
				frame = &vm.frames[vm.frameCount -1];
				break;
			}
			case OP_JUMP: {
				uint16_t offset = READ_SHORT();
				frame->ip += offset;
				break;
			}
			case OP_JUMP_IF_FALSE: {
				uint16_t offset = READ_SHORT();
				if (isFalsey(peek(0))) frame->ip += offset;
				break;
			}
			case OP_LOOP: {
				uint16_t offset = READ_SHORT();
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
					uint8_t isLocal = READ_BYTE();
					uint8_t index = READ_BYTE();
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
				Value result = pop();
				closeUpvalues(frame->slots);
				vm.frameCount--;
				if (vm.frameCount == 0) {
					pop();
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
				int count = READ_BYTE();
				ObjArray* array = newArray();
				
				if (count > 0) {
					/* Push array for GC protection before allocating */
					push(OBJ_VAL(array));
					
					/* Allocate space for all elements at once */
					array->elements = ALLOCATE(Value, count);
					array->capacity = count;
					array->count = count;
					
					/* Copy elements from stack (they're at peek(count), peek(count-1), ..., peek(1)) */
					for (int i = 0; i < count; i++) {
						array->elements[i] = peek(count - i);
					}
					
					/* Pop array temporarily */
					pop();
				}
				
				/* Pop all element values */
				for (int i = 0; i < count; i++) {
					pop();
				}
				
				/* Push the array back onto stack */
				push(OBJ_VAL(array));
				break;
			}
			case OP_INDEX_SUBSCR: {
				Value index = pop();
				Value array = pop();
				
				if (!IS_ARRAY(array)) {
					runtimeError("Can only index arrays.");
					return INTERPRET_RUNTIME_ERROR;
				}
				
				if (!IS_NUMBER(index)) {
					runtimeError("Array index must be a number.");
					return INTERPRET_RUNTIME_ERROR;
				}
				
				int idx = (int)AS_NUMBER(index);
				ObjArray* arr = AS_ARRAY(array);
				
				if (idx < 0 || idx >= arr->count) {
					runtimeError("Array index out of bounds.");
					return INTERPRET_RUNTIME_ERROR;
				}
				
				push(arr->elements[idx]);
				break;
			}
			case OP_STORE_SUBSCR: {
				Value value = pop();
				Value index = pop();
				Value array = pop();
				
				if (!IS_ARRAY(array)) {
					runtimeError("Can only index arrays.");
					return INTERPRET_RUNTIME_ERROR;
				}
				
				if (!IS_NUMBER(index)) {
					runtimeError("Array index must be a number.");
					return INTERPRET_RUNTIME_ERROR;
				}
				
				int idx = (int)AS_NUMBER(index);
				ObjArray* arr = AS_ARRAY(array);
				
				if (idx < 0) {
					runtimeError("Array index out of bounds.");
					return INTERPRET_RUNTIME_ERROR;
				}

				if (idx >= arr->count) {
					int oldCount = arr->count;
					int requiredCount = idx + 1;
					int oldCapacity = arr->capacity;

					while (arr->capacity < requiredCount) {
						arr->capacity = GROW_CAPACITY(arr->capacity);
					}

					if (arr->capacity != oldCapacity) {
						arr->elements = GROW_ARRAY(Value, arr->elements, oldCapacity, arr->capacity);
					}

					for (int i = oldCount; i < requiredCount; i++) {
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

InterpretResult interpret(const char* source) {
	ObjFunction* function = compile(source);
	if (function == NULL) return INTERPRET_COMPILE_ERROR;

	push (OBJ_VAL(function));
	ObjClosure* closure = newClosure(function);
	pop();
	push(OBJ_VAL(closure));
	call(closure, 0);

	return run();
}
