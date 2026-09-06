#include "lux.h"
#include "common.h"
#include "value.h"
#include "object.h"
#include "vm.h"
#include "table.h"
#include "ninep.h"
#include "ninep_native.h"

static ObjClass *ninepClass = nil;
static ObjClass *ninepConnClass = nil;

void
ninepSetClasses(ObjClass *server, ObjClass *conn)
{
	ninepClass = server;
	ninepConnClass = conn;
}

static void
setPtr(ObjInstance *inst, NinePClient *c)
{
	if (c == nil) {
		tableSet(&inst->fields, copyString("_ptr", 4), NIL_VAL);
		return;
	}
	tableSet(&inst->fields, copyString("_ptr", 4),
		NUMBER_VAL((double)(uintptr)c));
}

static NinePClient *
getClient(ObjInstance *inst)
{
	Value v;

	if (inst == nil || ninepConnClass == nil || inst->klass != ninepConnClass)
		return nil;
	if (!tableGet(&inst->fields, copyString("_ptr", 4), &v) || !IS_NUMBER(v))
		return nil;
	return (NinePClient*)(uintptr)AS_NUMBER(v);
}

static void
setErr(ObjInstance *inst, char *err)
{
	if (inst == nil)
		return;
	if (err == nil)
		err = "";
	tableSet(&inst->fields, copyString("err", 3),
		OBJ_VAL(copyString(err, strlen(err))));
}

void
ninepCloseFromInstance(ObjInstance *instance)
{
	NinePClient *c;

	if (instance == nil)
		return;
	c = getClient(instance);
	if (c == nil)
		return;
	ninep_client_close(c);
	setPtr(instance, nil);
}

static ObjArray *
ninepFiles(ObjInstance *fs)
{
	Value v;

	if (!tableGet(&fs->fields, copyString("_files", 6), &v) || !IS_ARRAY(v)) {
		ObjArray *arr = newArray();
		tableSet(&fs->fields, copyString("_files", 6), OBJ_VAL(arr));
		return arr;
	}
	return AS_ARRAY(v);
}

static int
localPath(ObjInstance *ent, char **path)
{
	Value v;

	if (!tableGet(&ent->fields, copyString("local", 5), &v) || !IS_STRING(v))
		return 0;
	*path = AS_CSTRING(v);
	return 1;
}

static int
readLocal(char *path, char **data, int *len)
{
	int fd;
	Dir *d;
	long n, got;
	char *buf;

	fd = open(path, OREAD);
	if (fd < 0)
		return -1;
	d = dirfstat(fd);
	if (d == nil) {
		close(fd);
		return -1;
	}
	n = d->length;
	free(d);
	if (n < 0 || n > 8 * 1024 * 1024) {
		close(fd);
		return -1;
	}
	buf = malloc(n + 1);
	if (buf == nil) {
		close(fd);
		return -1;
	}
	got = read(fd, buf, n);
	close(fd);
	if (got < 0) {
		free(buf);
		return -1;
	}
	buf[got] = 0;
	*data = buf;
	*len = (int)got;
	return 0;
}

static int
writeLocal(char *path, char *data, int len)
{
	int fd;
	long put;

	fd = create(path, OWRITE, 0666);
	if (fd < 0)
		return -1;
	if (len < 0)
		len = 0;
	put = write(fd, data, len);
	close(fd);
	return put == len ? 0 : -1;
}

static char *
parse9pname(char *path)
{
	char *name;

	if (path == nil)
		return nil;
	name = path;
	if (name[0] == '/')
		name++;
	if (name[0] == 0 || strchr(name, '/') != nil)
		return nil;
	return name;
}

static int
ninepOnRead(void *aux, char **data, int *len)
{
	ObjInstance *ent;
	Value fn, result;
	char *path;

	ent = aux;
	*data = nil;
	*len = 0;
	if (tableGet(&ent->fields, copyString("read", 4), &fn) && IS_CLOSURE(fn)) {
		if (!luxInvokeClosure(AS_CLOSURE(fn), 0, nil, &result))
			return -1;
		if (IS_NIL(result)) {
			*data = malloc(1);
			if (*data == nil)
				return -1;
			(*data)[0] = 0;
			*len = 0;
			return 0;
		}
		if (!IS_STRING(result))
			return -1;
		*len = AS_STRING(result)->length;
		*data = malloc(*len + 1);
		if (*data == nil)
			return -1;
		memcpy(*data, AS_CSTRING(result), *len);
		(*data)[*len] = 0;
		return 0;
	}
	if (localPath(ent, &path))
		return readLocal(path, data, len);
	return -1;
}

static int
ninepOnWrite(void *aux, char *data, int len)
{
	ObjInstance *ent;
	Value fn, arg, result;
	char *path;

	ent = aux;
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
ninepFill(ObjInstance *fs, NinePFile *files, int *nfiles)
{
	ObjArray *arr;
	int i, n;
	Value entVal, nameVal, readVal, writeVal, localVal;
	ObjInstance *ent;
	char *name;

	arr = ninepFiles(fs);
	n = 0;
	for (i = 0; i < arr->count && n < NINEP_MAXFILES; i++) {
		entVal = arr->elements[i];
		if (!IS_INSTANCE(entVal))
			continue;
		ent = AS_INSTANCE(entVal);
		if (!tableGet(&ent->fields, copyString("name", 4), &nameVal) || !IS_STRING(nameVal))
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

static char *
basename9(char *path)
{
	char *slash;

	slash = strrchr(path, '/');
	if (slash != nil && slash[1] != 0)
		return slash + 1;
	return path;
}

static int
ninepPostName(ObjInstance *fs, char *name)
{
	int p[2], srv, nfiles;
	char path[128], buf[32];
	NinePFile files[NINEP_MAXFILES];
	NinePOps ops;

	if (pipe(p) < 0)
		return -1;
	snprint(path, sizeof path, "/srv/%s", name);
	srv = create(path, OWRITE, 0600);
	if (srv < 0) {
		close(p[0]);
		close(p[1]);
		return -1;
	}
	snprint(buf, sizeof buf, "%d", p[0]);
	write(srv, buf, strlen(buf));
	close(srv);
	close(p[0]);
	ops.onread = ninepOnRead;
	ops.onwrite = ninepOnWrite;
	ninepFill(fs, files, &nfiles);
	print("9P posted at /srv/%s\n", name);
	ninep_serve(p[1], files, nfiles, &ops);
	close(p[1]);
	return 0;
}

static int
ninepServeTcp(ObjInstance *fs, char *addr)
{
	NinePListener lis;
	NinePFile files[NINEP_MAXFILES];
	NinePOps ops;
	int nfiles, client;

	if (ninep_listen(addr, &lis) < 0)
		return -1;
	print("9P listening on %s\n", addr);
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

static char *
argString(int argCount, Value *args)
{
	if (argCount < 1 || !IS_STRING(args[argCount - 1]))
		return nil;
	return AS_CSTRING(args[argCount - 1]);
}

Value
ninepInitNative(int argCount, Value *args)
{
	ObjInstance *inst;

	if (argCount != 1 || !IS_INSTANCE(args[0]))
		return NIL_VAL;
	inst = AS_INSTANCE(args[0]);
	tableSet(&inst->fields, copyString("_files", 6), OBJ_VAL(newArray()));
	return args[0];
}

Value
ninepFileNative(int argCount, Value *args)
{
	ObjInstance *fs, *ent;
	ObjArray *arr;
	char *path, *name;

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
	if (name[0] == 0 || strchr(name, '/') != nil)
		return BOOL_VAL(false);
	fs = AS_INSTANCE(args[0]);
	arr = ninepFiles(fs);
	ent = newInstance(nil);
	push(OBJ_VAL(ent));
	tableSet(&ent->fields, copyString("name", 4),
		OBJ_VAL(copyString(name, strlen(name))));
	tableSet(&ent->fields, copyString("read", 4), args[2]);
	tableSet(&ent->fields, copyString("write", 5), args[3]);
	writeArray(arr, OBJ_VAL(ent));
	pop();
	return BOOL_VAL(true);
}

Value
ninepExportNative(int argCount, Value *args)
{
	ObjInstance *fs, *ent;
	ObjArray *arr;
	char *name;
	ObjString *local;

	if (argCount != 3 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1]) ||
	    !IS_STRING(args[2]))
		return BOOL_VAL(false);
	name = parse9pname(AS_CSTRING(args[1]));
	if (name == nil)
		return BOOL_VAL(false);
	if (AS_STRING(args[2])->length == 0)
		return BOOL_VAL(false);
	fs = AS_INSTANCE(args[0]);
	arr = ninepFiles(fs);
	ent = newInstance(nil);
	push(OBJ_VAL(ent));
	local = AS_STRING(args[2]);
	tableSet(&ent->fields, copyString("name", 4),
		OBJ_VAL(copyString(name, strlen(name))));
	tableSet(&ent->fields, copyString("local", 5),
		OBJ_VAL(copyString(local->chars, local->length)));
	writeArray(arr, OBJ_VAL(ent));
	pop();
	return BOOL_VAL(true);
}

Value
ninepListenNative(int argCount, Value *args)
{
	char *path;

	if (argCount != 2 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1]))
		return BOOL_VAL(false);
	path = AS_CSTRING(args[1]);
	if (strncmp(path, "tcp!", 4) == 0) {
		if (ninepServeTcp(AS_INSTANCE(args[0]), path) < 0)
			return BOOL_VAL(false);
		return BOOL_VAL(true);
	}
	if (ninepPostName(AS_INSTANCE(args[0]), basename9(path)) < 0)
		return BOOL_VAL(false);
	return BOOL_VAL(true);
}

Value
ninepPostNative(int argCount, Value *args)
{
	char *name;

	if (argCount != 2 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1]))
		return BOOL_VAL(false);
	name = AS_CSTRING(args[1]);
	if (strchr(name, '/') != nil || name[0] == 0)
		return BOOL_VAL(false);
	if (ninepPostName(AS_INSTANCE(args[0]), name) < 0)
		return BOOL_VAL(false);
	return BOOL_VAL(true);
}

Value
ninepConnectNative(int argCount, Value *args)
{
	char *addr, *user;
	int fd;
	NinePClient *c;
	ObjInstance *inst;

	addr = argString(argCount, args);
	if (addr == nil || ninepConnClass == nil)
		return NIL_VAL;
	fd = ninep_dial(addr);
	if (fd < 0)
		return NIL_VAL;
	user = getuser();
	c = ninep_client_attach(fd, user, "");
	if (c == nil)
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
ninepReadNative(int argCount, Value *args)
{
	ObjInstance *inst;
	NinePClient *c;
	char *data;
	int len;
	Value result;

	data = nil;
	len = 0;
	if (argCount != 2 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1]))
		return NIL_VAL;
	inst = AS_INSTANCE(args[0]);
	c = getClient(inst);
	if (c == nil)
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
ninepWriteNative(int argCount, Value *args)
{
	ObjInstance *inst;
	NinePClient *c;
	char *data;
	int len;

	if (argCount != 3 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1]) ||
	    !IS_STRING(args[2]))
		return BOOL_VAL(false);
	inst = AS_INSTANCE(args[0]);
	c = getClient(inst);
	if (c == nil)
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
ninepGetNative(int argCount, Value *args)
{
	ObjInstance *inst;
	NinePClient *c;
	char *data;
	int len;

	data = nil;
	len = 0;
	if (argCount != 3 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1]) ||
	    !IS_STRING(args[2]))
		return BOOL_VAL(false);
	inst = AS_INSTANCE(args[0]);
	c = getClient(inst);
	if (c == nil)
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
ninepPutNative(int argCount, Value *args)
{
	ObjInstance *inst;
	NinePClient *c;
	char *data;
	int len;

	data = nil;
	len = 0;
	if (argCount != 3 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1]) ||
	    !IS_STRING(args[2]))
		return BOOL_VAL(false);
	inst = AS_INSTANCE(args[0]);
	c = getClient(inst);
	if (c == nil)
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
ninepLsNative(int argCount, Value *args)
{
	ObjInstance *inst;
	NinePClient *c;
	NinePList list;
	ObjArray *arr;
	int i;

	if (argCount != 2 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1]))
		return NIL_VAL;
	inst = AS_INSTANCE(args[0]);
	c = getClient(inst);
	if (c == nil)
		return NIL_VAL;
	if (ninep_client_ls(c, AS_CSTRING(args[1]), &list) < 0) {
		setErr(inst, ninep_client_err(c));
		return NIL_VAL;
	}
	arr = newArray();
	push(OBJ_VAL(arr));
	for (i = 0; i < list.count; i++)
		writeArray(arr, OBJ_VAL(copyString(list.names[i], strlen(list.names[i]))));
	ninep_list_free(&list);
	pop();
	setErr(inst, "");
	return OBJ_VAL(arr);
}

Value
ninepStatNative(int argCount, Value *args)
{
	ObjInstance *inst;
	NinePClient *c;
	NinePStat st;
	ObjInstance *out;
	char *typ;

	if (argCount != 2 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1]))
		return NIL_VAL;
	inst = AS_INSTANCE(args[0]);
	c = getClient(inst);
	if (c == nil)
		return NIL_VAL;
	if (ninep_client_stat(c, AS_CSTRING(args[1]), &st) < 0) {
		setErr(inst, ninep_client_err(c));
		return NIL_VAL;
	}
	out = newInstance(nil);
	push(OBJ_VAL(out));
	tableSet(&out->fields, copyString("name", 4),
		OBJ_VAL(copyString(st.name, strlen(st.name))));
	typ = st.isdir ? "dir" : "file";
	tableSet(&out->fields, copyString("type", 4),
		OBJ_VAL(copyString(typ, strlen(typ))));
	tableSet(&out->fields, copyString("length", 6), NUMBER_VAL((double)st.length));
	tableSet(&out->fields, copyString("mode", 4), NUMBER_VAL((double)st.mode));
	tableSet(&out->fields, copyString("uid", 3),
		OBJ_VAL(copyString(st.uid, strlen(st.uid))));
	tableSet(&out->fields, copyString("gid", 3),
		OBJ_VAL(copyString(st.gid, strlen(st.gid))));
	pop();
	setErr(inst, "");
	return OBJ_VAL(out);
}

Value
ninepCloseNative(int argCount, Value *args)
{
	if (argCount != 1 || !IS_INSTANCE(args[0]))
		return BOOL_VAL(false);
	ninepCloseFromInstance(AS_INSTANCE(args[0]));
	return BOOL_VAL(true);
}
