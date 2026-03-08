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
#include <libsec.h>

/* SHA-256 produces 32 bytes */
#ifndef SHA2_256dlen
#define SHA2_256dlen 32
#endif

VM vm;

/* posix linux only 
static Value 
clockNative(int argCount, Value* args)
{
	return NUMBER_VAL((double)clock() / CLOCK_PER_SEC);
}
*/

static Value 
clockNative(int argCount, Value* args)
{
    (void)argCount; (void)args;
    // Plan 9: use nsec() for nanoseconds since boot, divide by 1e9 for seconds
    return NUMBER_VAL((double)nsec() / 1000000000.0);
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
			sprint(key, "%d", index);
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
	if (argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1]))
		return NIL_VAL;
	
	char* xml = AS_CSTRING(args[0]);
	char* tagName = AS_CSTRING(args[1]);
	
	/* Build open and close tags */
	char openTag[256];
	char closeTag[256];
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
	
	if (write(fd, request, strlen(request)) < 0) {
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

/* httpRequest(method, url, [body], [headers]) -> string or nil
 * method: "GET", "POST", "PUT", "DELETE", etc.
 * url: target URL
 * body: optional request body (nil or string)
 * headers: optional instance with header fields (nil or instance)
 */
static Value
httpRequestNative(int argCount, Value* args)
{
	if (argCount < 2 || argCount > 4)
		return NIL_VAL;
	
	if (!IS_STRING(args[0]) || !IS_STRING(args[1]))
		return NIL_VAL;
	
	char* method = AS_CSTRING(args[0]);
	char* url = AS_CSTRING(args[1]);
	char* requestBody = nil;
	int requestBodyLen = 0;
	ObjInstance* headersObj = nil;
	
	/* Optional body (arg 2) */
	if (argCount >= 3 && !IS_NIL(args[2])) {
		if (!IS_STRING(args[2]))
			return NIL_VAL;
		requestBody = AS_CSTRING(args[2]);
		requestBodyLen = strlen(requestBody);
	}
	
	/* Optional headers (arg 3) */
	if (argCount >= 4 && !IS_NIL(args[3])) {
		if (!IS_INSTANCE(args[3]))
			return NIL_VAL;
		headersObj = AS_INSTANCE(args[3]);
	}
	
	UrlParts parts;
	if (parseUrl(url, &parts) < 0)
		return NIL_VAL;
	
	int fd;
	TLSconn conn;
	
	/* Dial */
	char dialAddr[512];
	snprint(dialAddr, sizeof(dialAddr), "tcp!%s!%s", parts.host, parts.port);
	
	if (parts.ishttps) {
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
		fd = dial(dialAddr, nil, nil, nil);
		if (fd < 0)
			return NIL_VAL;
	}
	
	/* Build HTTP request with custom headers */
	char request[8192];
	int reqLen = snprint(request, sizeof(request),
		"%s %s HTTP/1.1\r\n"
		"Host: %s\r\n"
		"User-Agent: lux/1.0\r\n",
		method, parts.path, parts.host);
	
	/* Add custom headers if provided */
	if (headersObj != nil) {
		for (int i = 0; i < headersObj->fields.capacity; i++) {
			if (headersObj->fields.entries[i].key != nil) {
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
					
					reqLen += snprint(request + reqLen, sizeof(request) - reqLen,
						"%s: %s\r\n", convertedName, AS_CSTRING(headerValue));
				}
			}
		}
	}
	
	/* Add Content-Length if body is present */
	if (requestBody != nil && requestBodyLen > 0) {
		reqLen += snprint(request + reqLen, sizeof(request) - reqLen,
			"Content-Length: %d\r\n", requestBodyLen);
	}
	
	/* Close headers */
	reqLen += snprint(request + reqLen, sizeof(request) - reqLen,
		"Connection: close\r\n\r\n");
	
	/* Write headers */
	if (write(fd, request, reqLen) < 0) {
		if (parts.ishttps)
			free(conn.cert);
		close(fd);
		return NIL_VAL;
	}
	
	/* Write body if present */
	if (requestBody != nil && requestBodyLen > 0) {
		if (write(fd, requestBody, requestBodyLen) < 0) {
			if (parts.ishttps)
				free(conn.cert);
			close(fd);
			return NIL_VAL;
		}
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
	char request[4096];
	int reqLen = snprint(request, sizeof(request),
		"POST %s HTTP/1.0\r\n"
		"Host: %s\r\n"
		"User-Agent: lux/1.0\r\n"
		"Content-Type: application/json\r\n"
		"Content-Length: %d\r\n"
		"Connection: close\r\n"
		"\r\n",
		parts.path, parts.host, postBodyLen);
	
	if (write(fd, request, reqLen) < 0) {
		if (parts.ishttps)
			free(conn.cert);
		close(fd);
		return NIL_VAL;
	}
	
	if (write(fd, postBody, postBodyLen) < 0) {
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
	char request[4096];
	int reqLen = snprint(request, sizeof(request),
		"PUT %s HTTP/1.0\r\n"
		"Host: %s\r\n"
		"User-Agent: lux/1.0\r\n"
		"Content-Type: application/json\r\n"
		"Content-Length: %d\r\n"
		"Connection: close\r\n"
		"\r\n",
		parts.path, parts.host, putBodyLen);
	
	if (write(fd, request, reqLen) < 0) {
		if (parts.ishttps)
			free(conn.cert);
		close(fd);
		return NIL_VAL;
	}
	
	if (write(fd, putBody, putBodyLen) < 0) {
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
	char body[4096];
	int bodyLen;
} HttpRequest;

static int
parseHttpRequest(char* buffer, int bufLen, HttpRequest* req)
{
	char* p = buffer;
	
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
	char* bodyStart = strstr(buffer, "\r\n\r\n");
	if (bodyStart != nil) {
		bodyStart += 4;
	} else {
		bodyStart = strstr(buffer, "\n\n");
		if (bodyStart != nil)
			bodyStart += 2;
		else
			bodyStart = nil;
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

static void
sendHttpResponse(int fd, int statusCode, char* statusText, char* body)
{
	char response[8192];
	int bodyLen = strlen(body);
	
	int len = snprint(response, sizeof(response),
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

/* Create AWS Signature V4 */
static void
createAwsSignature(char* method, char* host, char* uri, char* queryString,
		char* payloadHash, char* accessKey, char* secretKey, char* region,
		char* service, char* amzDate, char* dateStamp, char* sessionToken,
		char* authHeader, int authHeaderLen)
{
	/* Canonical request */
	char canonicalHeaders[1024];
	snprint(canonicalHeaders, sizeof(canonicalHeaders),
		"host:%s\nx-amz-date:%s\n", host, amzDate);
	
	char signedHeaders[256];
	if (sessionToken && sessionToken[0]) {
		/* Include session token in canonical headers */
		char tokHeader[512];
		snprint(tokHeader, sizeof(tokHeader), "x-amz-security-token:%s\n", sessionToken);
		strncat(canonicalHeaders, tokHeader, sizeof(canonicalHeaders) - strlen(canonicalHeaders) - 1);
		snprint(signedHeaders, sizeof(signedHeaders), "host;x-amz-date;x-amz-security-token");
	} else {
		snprint(signedHeaders, sizeof(signedHeaders), "host;x-amz-date");
	}
	
	char canonicalRequest[4096];
	snprint(canonicalRequest, sizeof(canonicalRequest),
		"%s\n%s\n%s\n%s\n%s\n%s",
		method, uri, queryString, canonicalHeaders, signedHeaders, payloadHash);
	
	/* Hash canonical request */
	uchar canonicalHash[SHA2_256dlen];
	sha256Hash((uchar*)canonicalRequest, strlen(canonicalRequest), canonicalHash);
	char canonicalHashHex[SHA2_256dlen*2+1];
	hexEncode(canonicalHash, SHA2_256dlen, canonicalHashHex);
	
	/* String to sign */
	char credentialScope[256];
	snprint(credentialScope, sizeof(credentialScope),
		"%s/%s/%s/aws4_request", dateStamp, region, service);
	
	char stringToSign[4096];
	snprint(stringToSign, sizeof(stringToSign),
		"AWS4-HMAC-SHA256\n%s\n%s\n%s",
		amzDate, credentialScope, canonicalHashHex);
	
	/* Calculate signature */
	uchar signingKey[SHA2_256dlen];
	getAwsSigningKey(secretKey, dateStamp, region, service, signingKey);
	
	uchar signature[SHA2_256dlen];
	hmacSha256(signingKey, SHA2_256dlen, (uchar*)stringToSign, strlen(stringToSign), signature);
	
	char signatureHex[SHA2_256dlen*2+1];
	hexEncode(signature, SHA2_256dlen, signatureHex);
	
	/* Create authorization header */
	snprint(authHeader, authHeaderLen,
		"AWS4-HMAC-SHA256 Credential=%s/%s, SignedHeaders=%s, Signature=%s",
		accessKey, credentialScope, signedHeaders, signatureHex);
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
		/* Simple URL encoding for prefix */
		int j = 0;
		for (int i = 0; prefix[i] && j < sizeof(encodedPrefix)-4; i++) {
			char c = prefix[i];
			if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || 
			    (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
				encodedPrefix[j++] = c;
			} else {
				snprint(encodedPrefix+j, 4, "%%%02X", (unsigned char)c);
				j += 3;
			}
		}
		encodedPrefix[j] = '\0';
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
	char request[4096];
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
	char request[2048];
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
	char request[2048];
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
	
	write(fd, request, reqLen);
	write(fd, content, contentLen);
	
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
		int totalRead = 0;
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
		char responseBody[4096];
		if (strcmp(req.path, "/") == 0) {
			snprint(responseBody, sizeof(responseBody),
				"{\"message\":\"Hello from Lux!\",\"server\":\"lux/1.0\"}");
			sendHttpResponse(dfd, 200, "OK", responseBody);
		} else if (strcmp(req.path, "/echo") == 0) {
			snprint(responseBody, sizeof(responseBody),
				"{\"method\":\"%s\",\"path\":\"%s\",\"body\":\"%s\"}",
				req.method, req.path, req.body);
			sendHttpResponse(dfd, 200, "OK", responseBody);
		} else {
			snprint(responseBody, sizeof(responseBody),
				"{\"error\":\"Not Found\",\"path\":\"%s\"}", req.path);
			sendHttpResponse(dfd, 404, "Not Found", responseBody);
		}
		
		close(dfd);
	}
	
	close(afd);
	return BOOL_VAL(true);
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


	initTable(&vm.globals);
	initTable(&vm.strings);
	initTable(&vm.imports);

	vm.initString = nil;
	vm.initString = copyString("init", 4);

	defineNative("clock", clockNative);
	defineNative("readFile", readFileNative);
	defineNative("writeFile", writeFileNative);
	defineNative("appendFile", appendFileNative);
	defineNative("deleteFile", deleteFileNative);
	defineNative("fileExists", fileExistsNative);
	defineNative("createDir", createDirNative);
	defineNative("listDir", listDirNative);
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
				}
				return true;
			}
			case OBJ_CLOSURE:
				return call(AS_CLOSURE(callee), argCount);
			case OBJ_NATIVE: {
			    NativeFn native;
                Value result;
                native = AS_NATIVE(callee);
				result = native(argCount, vm.stackTop - argCount);
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

static bool 
invokeFromClass(ObjClass* klass, ObjString* name, int argCount)
{
	Value method;
	if (!tableGet(&klass->methods, name, &method)) {
		runtimeError("Undefined property '%s'.", name->chars);
		return false;
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
	CallFrame* frame = &vm.frames[vm.frameCount -1];
	uchar instruction;
	Value a, b, constant;
	double da, db;

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
				runtimeError("Operands must be numbers.");
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
			} else {
				/* At least one is not a number, convert both to strings and concatenate */
				b = pop();
				a = pop();
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
			unsigned char offset = READ_SHORT(); // uint16_t in posix linux
			frame->ip += offset;
			break;			
		}

		case OP_JUMP_IF_FALSE: {
			unsigned char offset = READ_SHORT();
			if (isFalsey(peek(0))) frame->ip += offset;
			break;
		}
		
		case OP_LOOP: {
			unsigned char offset = READ_SHORT();
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
			Value array;
			int idx;
			ObjArray* arr;
			
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
			
			if (idx < 0 || idx >= arr->count) {
				runtimeError("Array index out of bounds.");
				return INTERPRET_RUNTIME_ERROR;
			}
			
			push(arr->elements[idx]);
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
