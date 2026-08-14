#include "lux.h"
#include "common.h"
#include "value.h"
#include "object.h"
#include "vm.h"
#include "table.h"
#include "ninep.h"

static ObjArray*
ninepFiles(ObjInstance *fs)
{
	Value v;

	if(!tableGet(&fs->fields, copyString("_files", 6), &v) || !IS_ARRAY(v)){
		ObjArray *arr = newArray();
		tableSet(&fs->fields, copyString("_files", 6), OBJ_VAL(arr));
		return arr;
	}
	return AS_ARRAY(v);
}

static int
ninepOnRead(void *aux, char **data, int *len)
{
	ObjInstance *ent;
	Value fn, result;

	ent = (ObjInstance*)aux;
	*data = nil;
	*len = 0;
	if(!tableGet(&ent->fields, copyString("read", 4), &fn) || !IS_CLOSURE(fn))
		return -1;
	if(!luxInvokeClosure(AS_CLOSURE(fn), 0, nil, &result))
		return -1;
	if(IS_NIL(result)){
		*data = malloc(1);
		if(*data == nil)
			return -1;
		(*data)[0] = 0;
		*len = 0;
		return 0;
	}
	if(!IS_STRING(result))
		return -1;
	*len = AS_STRING(result)->length;
	*data = malloc(*len + 1);
	if(*data == nil)
		return -1;
	memcpy(*data, AS_CSTRING(result), *len);
	(*data)[*len] = 0;
	return 0;
}

static int
ninepOnWrite(void *aux, char *data, int len)
{
	ObjInstance *ent;
	Value fn, arg, result;

	ent = (ObjInstance*)aux;
	if(!tableGet(&ent->fields, copyString("write", 5), &fn) || !IS_CLOSURE(fn))
		return -1;
	arg = OBJ_VAL(copyString(data, len));
	if(!luxInvokeClosure(AS_CLOSURE(fn), 1, &arg, &result))
		return -1;
	if(IS_BOOL(result) && !AS_BOOL(result))
		return -1;
	return 0;
}

static int
ninepFill(ObjInstance *fs, NinePFile *files, int *nfiles)
{
	ObjArray *arr;
	int i, n;
	Value entVal, nameVal, readVal, writeVal;
	ObjInstance *ent;
	char *name;

	arr = ninepFiles(fs);
	n = 0;
	for(i = 0; i < arr->count && n < NINEP_MAXFILES; i++){
		entVal = arr->elements[i];
		if(!IS_INSTANCE(entVal))
			continue;
		ent = AS_INSTANCE(entVal);
		if(!tableGet(&ent->fields, copyString("name", 4), &nameVal) || !IS_STRING(nameVal))
			continue;
		name = AS_CSTRING(nameVal);
		if(strlen(name) >= sizeof(files[n].name))
			continue;
		memset(&files[n], 0, sizeof(files[n]));
		strncpy(files[n].name, name, sizeof(files[n].name)-1);
		tableGet(&ent->fields, copyString("read", 4), &readVal);
		tableGet(&ent->fields, copyString("write", 5), &writeVal);
		files[n].readable = IS_CLOSURE(readVal) ? 1 : 0;
		files[n].writable = IS_CLOSURE(writeVal) ? 1 : 0;
		files[n].aux = ent;
		n++;
	}
	*nfiles = n;
	return 0;
}

static char*
basename9(char *path)
{
	char *s, *slash;

	s = path;
	slash = strrchr(s, '/');
	if(slash != nil && slash[1] != 0)
		return slash+1;
	return s;
}

static int
ninepPostName(ObjInstance *fs, char *name)
{
	int p[2], srv, nfiles;
	char path[128], buf[32];
	NinePFile files[NINEP_MAXFILES];
	NinePOps ops;

	if(pipe(p) < 0)
		return -1;
	snprint(path, sizeof path, "/srv/%s", name);
	srv = create(path, OWRITE, 0600);
	if(srv < 0){
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
	ninep_serve(p[1], files, nfiles, &ops);
	close(p[1]);
	return 0;
}

Value
ninepInitNative(int argCount, Value *args)
{
	ObjInstance *inst;

	if(argCount != 1 || !IS_INSTANCE(args[0]))
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

	if(argCount != 4 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1]))
		return BOOL_VAL(false);
	if(!IS_NIL(args[2]) && !IS_CLOSURE(args[2]))
		return BOOL_VAL(false);
	if(!IS_NIL(args[3]) && !IS_CLOSURE(args[3]))
		return BOOL_VAL(false);
	path = AS_CSTRING(args[1]);
	name = path;
	if(name[0] == '/')
		name++;
	if(name[0] == 0 || strchr(name, '/') != nil)
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
ninepListenNative(int argCount, Value *args)
{
	char *path;

	if(argCount != 2 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1]))
		return BOOL_VAL(false);
	path = AS_CSTRING(args[1]);
	if(ninepPostName(AS_INSTANCE(args[0]), basename9(path)) < 0)
		return BOOL_VAL(false);
	return BOOL_VAL(true);
}

Value
ninepPostNative(int argCount, Value *args)
{
	char *name;

	if(argCount != 2 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1]))
		return BOOL_VAL(false);
	name = AS_CSTRING(args[1]);
	if(strchr(name, '/') != nil)
		return BOOL_VAL(false);
	if(ninepPostName(AS_INSTANCE(args[0]), name) < 0)
		return BOOL_VAL(false);
	return BOOL_VAL(true);
}
