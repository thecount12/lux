#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "value.h"
#include "object.h"
#include "vm.h"
#include "table.h"
#include "ninep.h"
#include "ninep_native.h"

static ObjClass* ninepClass = NULL;
static ObjClass* ninepConnClass = NULL;

void
ninepSetClasses(ObjClass* server, ObjClass* conn)
{
	ninepClass = server;
	ninepConnClass = conn;
}

static void
setPtr(ObjInstance* inst, NinePClient* c)
{
	if (c == NULL) {
		tableSet(&inst->fields, copyString("_ptr", 4), NIL_VAL);
		return;
	}
	tableSet(&inst->fields, copyString("_ptr", 4),
		NUMBER_VAL((double)(uintptr_t)c));
}

static NinePClient*
getClient(ObjInstance* inst)
{
	Value v;

	if (inst == NULL || ninepConnClass == NULL || inst->klass != ninepConnClass)
		return NULL;
	if (!tableGet(&inst->fields, copyString("_ptr", 4), &v) || !IS_NUMBER(v))
		return NULL;
	return (NinePClient*)(uintptr_t)AS_NUMBER(v);
}

static void
setErr(ObjInstance* inst, const char* err)
{
	if (inst == NULL)
		return;
	if (err == NULL)
		err = "";
	tableSet(&inst->fields, copyString("err", 3),
		OBJ_VAL(copyString(err, (int)strlen(err))));
}

void
ninepCloseFromInstance(ObjInstance* instance)
{
	NinePClient* c;

	if (instance == NULL)
		return;
	c = getClient(instance);
	if (c == NULL)
		return;
	ninep_client_close(c);
	setPtr(instance, NULL);
}

static ObjArray*
ninepFiles(ObjInstance* fs)
{
	Value v;

	if (!tableGet(&fs->fields, copyString("_files", 6), &v) || !IS_ARRAY(v)) {
		ObjArray* arr = newArray();
		tableSet(&fs->fields, copyString("_files", 6), OBJ_VAL(arr));
		return arr;
	}
	return AS_ARRAY(v);
}

static int
localPath(ObjInstance* ent, const char** path)
{
	Value v;

	if (!tableGet(&ent->fields, copyString("local", 5), &v) || !IS_STRING(v))
		return 0;
	*path = AS_CSTRING(v);
	return 1;
}

static int
readLocal(const char* path, char** data, int* len)
{
	FILE* fp;
	long n;
	char* buf;

	fp = fopen(path, "rb");
	if (fp == NULL)
		return -1;
	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return -1;
	}
	n = ftell(fp);
	if (n < 0 || n > 8 * 1024 * 1024) {
		fclose(fp);
		return -1;
	}
	rewind(fp);
	buf = (char*)malloc((size_t)n + 1);
	if (buf == NULL) {
		fclose(fp);
		return -1;
	}
	if (n > 0 && fread(buf, 1, (size_t)n, fp) != (size_t)n) {
		free(buf);
		fclose(fp);
		return -1;
	}
	buf[n] = '\0';
	fclose(fp);
	*data = buf;
	*len = (int)n;
	return 0;
}

static int
writeLocal(const char* path, const char* data, int len)
{
	FILE* fp;
	size_t n;

	fp = fopen(path, "wb");
	if (fp == NULL)
		return -1;
	if (len < 0)
		len = 0;
	n = fwrite(data, 1, (size_t)len, fp);
	fclose(fp);
	return n == (size_t)len ? 0 : -1;
}

static const char*
parse9pname(const char* path)
{
	const char* name;

	if (path == NULL)
		return NULL;
	name = path;
	if (name[0] == '/')
		name++;
	if (name[0] == '\0' || strchr(name, '/') != NULL)
		return NULL;
	return name;
}

static int
ninepOnRead(void* aux, char** data, int* len)
{
	ObjInstance* ent = (ObjInstance*)aux;
	Value fn;
	Value result;
	const char* path;

	*data = NULL;
	*len = 0;
	if (tableGet(&ent->fields, copyString("read", 4), &fn) && IS_CLOSURE(fn)) {
		if (!luxInvokeClosure(AS_CLOSURE(fn), 0, NULL, &result))
			return -1;
		if (IS_NIL(result)) {
			*data = (char*)malloc(1);
			if (*data == NULL)
				return -1;
			(*data)[0] = '\0';
			*len = 0;
			return 0;
		}
		if (!IS_STRING(result))
			return -1;
		*len = AS_STRING(result)->length;
		*data = (char*)malloc((size_t)(*len) + 1);
		if (*data == NULL)
			return -1;
		memcpy(*data, AS_CSTRING(result), (size_t)(*len));
		(*data)[*len] = '\0';
		return 0;
	}
	if (localPath(ent, &path))
		return readLocal(path, data, len);
	return -1;
}

static int
ninepOnWrite(void* aux, const char* data, int len)
{
	ObjInstance* ent = (ObjInstance*)aux;
	Value fn;
	Value arg;
	Value result;
	const char* path;

	if (tableGet(&ent->fields, copyString("write", 5), &fn) && IS_CLOSURE(fn)) {
		arg = OBJ_VAL(copyString(data, len));
		if (!luxInvokeClosure(AS_CLOSURE(fn), 1, &arg, &result))
			return -1;
		if (IS_BOOL(result) && !AS_BOOL(result))
			return -1;
		return 0;
	}
	if (localPath(ent, &path))
		return writeLocal(path, data, len);
	return -1;
}

static int
ninepFill(ObjInstance* fs, NinePFile* files, int* nfiles)
{
	ObjArray* arr = ninepFiles(fs);
	int i;
	int n;

	n = 0;
	for (i = 0; i < arr->count && n < NINEP_MAXFILES; i++) {
		Value entVal = arr->elements[i];
		Value nameVal;
		Value readVal;
		Value writeVal;
		Value localVal;
		ObjInstance* ent;
		const char* name;

		if (!IS_INSTANCE(entVal))
			continue;
		ent = AS_INSTANCE(entVal);
		if (!tableGet(&ent->fields, copyString("name", 4), &nameVal) ||
		    !IS_STRING(nameVal))
			continue;
		name = AS_CSTRING(nameVal);
		if (strlen(name) >= sizeof(files[n].name))
			continue;
		memset(&files[n], 0, sizeof(files[n]));
		strncpy(files[n].name, name, sizeof(files[n].name) - 1);
		tableGet(&ent->fields, copyString("read", 4), &readVal);
		tableGet(&ent->fields, copyString("write", 5), &writeVal);
		if (!tableGet(&ent->fields, copyString("local", 5), &localVal))
			localVal = NIL_VAL;
		files[n].readable = (IS_CLOSURE(readVal) || IS_STRING(localVal)) ? 1 : 0;
		files[n].writable = (IS_CLOSURE(writeVal) || IS_STRING(localVal)) ? 1 : 0;
		files[n].aux = ent;
		n++;
	}
	*nfiles = n;
	return 0;
}

static int
ninepServeAddr(ObjInstance* fs, const char* addr)
{
	NinePListener lis;
	NinePFile files[NINEP_MAXFILES];
	NinePOps ops;
	int nfiles;
	int client;
	int port;

	if (ninep_listen(addr, &lis) < 0)
		return -1;
	port = ninep_bound_port(&lis);
	if (lis.tcp && port > 0)
		fprintf(stdout, "9P listening on tcp port %d\n", port);
	else if (!lis.tcp)
		fprintf(stdout, "9P listening on %s\n", lis.path);
	else
		fprintf(stdout, "9P listening on %s\n", addr);
	fflush(stdout);

	ops.onread = ninepOnRead;
	ops.onwrite = ninepOnWrite;
	for (;;) {
		client = ninep_accept(&lis);
		if (client < 0)
			break;
		ninepFill(fs, files, &nfiles);
		ninep_serve(client, files, nfiles, &ops);
		close(client);
	}
	ninep_unlisten(&lis);
	return 0;
}

static const char*
argString(int argCount, Value* args)
{
	if (argCount < 1)
		return NULL;
	if (!IS_STRING(args[argCount - 1]))
		return NULL;
	return AS_CSTRING(args[argCount - 1]);
}

Value
ninepInitNative(int argCount, Value* args)
{
	ObjInstance* inst;

	if (argCount != 1 || !IS_INSTANCE(args[0]))
		return NIL_VAL;
	inst = AS_INSTANCE(args[0]);
	tableSet(&inst->fields, copyString("_files", 6), OBJ_VAL(newArray()));
	return args[0];
}

Value
ninepFileNative(int argCount, Value* args)
{
	ObjInstance* fs;
	ObjInstance* ent;
	ObjArray* arr;
	const char* path;
	const char* name;

	if (argCount != 4 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1]))
		return BOOL_VAL(false);
	if (!IS_NIL(args[2]) && !IS_CLOSURE(args[2]))
		return BOOL_VAL(false);
	if (!IS_NIL(args[3]) && !IS_CLOSURE(args[3]))
		return BOOL_VAL(false);
	path = AS_CSTRING(args[1]);
	name = path;
	if (name[0] == '/')
		name++;
	if (name[0] == '\0' || strchr(name, '/') != NULL)
		return BOOL_VAL(false);
	fs = AS_INSTANCE(args[0]);
	arr = ninepFiles(fs);
	ent = newInstance(NULL);
	push(OBJ_VAL(ent));
	tableSet(&ent->fields, copyString("name", 4),
		OBJ_VAL(copyString(name, (int)strlen(name))));
	tableSet(&ent->fields, copyString("read", 4), args[2]);
	tableSet(&ent->fields, copyString("write", 5), args[3]);
	writeArray(arr, OBJ_VAL(ent));
	pop();
	return BOOL_VAL(true);
}

Value
ninepExportNative(int argCount, Value* args)
{
	ObjInstance* fs;
	ObjInstance* ent;
	ObjArray* arr;
	const char* name;
	ObjString* local;

	if (argCount != 3 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1]) ||
	    !IS_STRING(args[2]))
		return BOOL_VAL(false);
	name = parse9pname(AS_CSTRING(args[1]));
	if (name == NULL)
		return BOOL_VAL(false);
	if (AS_STRING(args[2])->length == 0)
		return BOOL_VAL(false);
	fs = AS_INSTANCE(args[0]);
	arr = ninepFiles(fs);
	ent = newInstance(NULL);
	push(OBJ_VAL(ent));
	local = AS_STRING(args[2]);
	tableSet(&ent->fields, copyString("name", 4),
		OBJ_VAL(copyString(name, (int)strlen(name))));
	tableSet(&ent->fields, copyString("local", 5),
		OBJ_VAL(copyString(local->chars, local->length)));
	writeArray(arr, OBJ_VAL(ent));
	pop();
	return BOOL_VAL(true);
}

Value
ninepListenNative(int argCount, Value* args)
{
	if (argCount != 2 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1]))
		return BOOL_VAL(false);
	if (ninepServeAddr(AS_INSTANCE(args[0]), AS_CSTRING(args[1])) < 0)
		return BOOL_VAL(false);
	return BOOL_VAL(true);
}

Value
ninepPostNative(int argCount, Value* args)
{
	char path[256];
	const char* name;

	if (argCount != 2 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1]))
		return BOOL_VAL(false);
	name = AS_CSTRING(args[1]);
	if (strchr(name, '/') != NULL || name[0] == '\0')
		return BOOL_VAL(false);
	snprintf(path, sizeof(path), "/tmp/%s.9p", name);
	if (ninepServeAddr(AS_INSTANCE(args[0]), path) < 0)
		return BOOL_VAL(false);
	return BOOL_VAL(true);
}

Value
ninepConnectNative(int argCount, Value* args)
{
	const char* addr;
	const char* user;
	int fd;
	NinePClient* c;
	ObjInstance* inst;

	addr = argString(argCount, args);
	if (addr == NULL || ninepConnClass == NULL)
		return NIL_VAL;
	fd = ninep_dial(addr);
	if (fd < 0)
		return NIL_VAL;
	user = getenv("USER");
	c = ninep_client_attach(fd, user, "");
	if (c == NULL)
		return NIL_VAL;
	inst = newInstance(ninepConnClass);
	push(OBJ_VAL(inst));
	setPtr(inst, c);
	setErr(inst, "");
	tableSet(&inst->fields, copyString("ok", 2), BOOL_VAL(true));
	pop();
	return OBJ_VAL(inst);
}

Value
ninepReadNative(int argCount, Value* args)
{
	ObjInstance* inst;
	NinePClient* c;
	char* data = NULL;
	int len = 0;
	Value result;

	if (argCount != 2 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1]))
		return NIL_VAL;
	inst = AS_INSTANCE(args[0]);
	c = getClient(inst);
	if (c == NULL)
		return NIL_VAL;
	if (ninep_client_read(c, AS_CSTRING(args[1]), &data, &len) < 0) {
		setErr(inst, ninep_client_err(c));
		return NIL_VAL;
	}
	result = OBJ_VAL(copyString(data, len));
	free(data);
	setErr(inst, "");
	return result;
}

Value
ninepWriteNative(int argCount, Value* args)
{
	ObjInstance* inst;
	NinePClient* c;
	const char* data;
	int len;

	if (argCount != 3 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1]) ||
	    !IS_STRING(args[2]))
		return BOOL_VAL(false);
	inst = AS_INSTANCE(args[0]);
	c = getClient(inst);
	if (c == NULL)
		return BOOL_VAL(false);
	data = AS_CSTRING(args[2]);
	len = AS_STRING(args[2])->length;
	if (ninep_client_write(c, AS_CSTRING(args[1]), data, len) < 0) {
		setErr(inst, ninep_client_err(c));
		return BOOL_VAL(false);
	}
	setErr(inst, "");
	return BOOL_VAL(true);
}

Value
ninepGetNative(int argCount, Value* args)
{
	ObjInstance* inst;
	NinePClient* c;
	char* data = NULL;
	int len = 0;

	if (argCount != 3 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1]) ||
	    !IS_STRING(args[2]))
		return BOOL_VAL(false);
	inst = AS_INSTANCE(args[0]);
	c = getClient(inst);
	if (c == NULL)
		return BOOL_VAL(false);
	if (ninep_client_read(c, AS_CSTRING(args[1]), &data, &len) < 0) {
		setErr(inst, ninep_client_err(c));
		return BOOL_VAL(false);
	}
	if (writeLocal(AS_CSTRING(args[2]), data, len) < 0) {
		free(data);
		setErr(inst, "write local file");
		return BOOL_VAL(false);
	}
	free(data);
	setErr(inst, "");
	return BOOL_VAL(true);
}

Value
ninepPutNative(int argCount, Value* args)
{
	ObjInstance* inst;
	NinePClient* c;
	char* data = NULL;
	int len = 0;

	if (argCount != 3 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1]) ||
	    !IS_STRING(args[2]))
		return BOOL_VAL(false);
	inst = AS_INSTANCE(args[0]);
	c = getClient(inst);
	if (c == NULL)
		return BOOL_VAL(false);
	if (readLocal(AS_CSTRING(args[1]), &data, &len) < 0) {
		setErr(inst, "read local file");
		return BOOL_VAL(false);
	}
	if (ninep_client_write(c, AS_CSTRING(args[2]), data, len) < 0) {
		free(data);
		setErr(inst, ninep_client_err(c));
		return BOOL_VAL(false);
	}
	free(data);
	setErr(inst, "");
	return BOOL_VAL(true);
}

Value
ninepLsNative(int argCount, Value* args)
{
	ObjInstance* inst;
	NinePClient* c;
	NinePList list;
	ObjArray* arr;
	int i;
	const char* path;

	if (argCount != 2 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1]))
		return NIL_VAL;
	inst = AS_INSTANCE(args[0]);
	c = getClient(inst);
	if (c == NULL)
		return NIL_VAL;
	path = AS_CSTRING(args[1]);
	if (ninep_client_ls(c, path, &list) < 0) {
		setErr(inst, ninep_client_err(c));
		return NIL_VAL;
	}
	arr = newArray();
	push(OBJ_VAL(arr));
	for (i = 0; i < list.count; i++)
		writeArray(arr, OBJ_VAL(copyString(list.names[i],
			(int)strlen(list.names[i]))));
	ninep_list_free(&list);
	pop();
	setErr(inst, "");
	return OBJ_VAL(arr);
}

Value
ninepStatNative(int argCount, Value* args)
{
	ObjInstance* inst;
	NinePClient* c;
	NinePStat st;
	ObjInstance* out;
	const char* typ;

	if (argCount != 2 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1]))
		return NIL_VAL;
	inst = AS_INSTANCE(args[0]);
	c = getClient(inst);
	if (c == NULL)
		return NIL_VAL;
	if (ninep_client_stat(c, AS_CSTRING(args[1]), &st) < 0) {
		setErr(inst, ninep_client_err(c));
		return NIL_VAL;
	}
	out = newInstance(NULL);
	push(OBJ_VAL(out));
	tableSet(&out->fields, copyString("name", 4),
		OBJ_VAL(copyString(st.name, (int)strlen(st.name))));
	typ = st.isdir ? "dir" : "file";
	tableSet(&out->fields, copyString("type", 4),
		OBJ_VAL(copyString(typ, (int)strlen(typ))));
	tableSet(&out->fields, copyString("length", 6),
		NUMBER_VAL((double)st.length));
	tableSet(&out->fields, copyString("mode", 4),
		NUMBER_VAL((double)st.mode));
	tableSet(&out->fields, copyString("uid", 3),
		OBJ_VAL(copyString(st.uid, (int)strlen(st.uid))));
	tableSet(&out->fields, copyString("gid", 3),
		OBJ_VAL(copyString(st.gid, (int)strlen(st.gid))));
	pop();
	setErr(inst, "");
	return OBJ_VAL(out);
}

Value
ninepCloseNative(int argCount, Value* args)
{
	if (argCount != 1 || !IS_INSTANCE(args[0]))
		return BOOL_VAL(false);
	ninepCloseFromInstance(AS_INSTANCE(args[0]));
	return BOOL_VAL(true);
}
