#include "lux.h"
#include "common.h"
#include "value.h"
#include "object.h"
#include "vm.h"
#include "table.h"
#include "dict.h"

static Dict*
getDictFromInstance(Value instVal)
{
	if (!IS_INSTANCE(instVal)) return nil;
	ObjInstance* inst = AS_INSTANCE(instVal);
	Value v;
	if (!tableGet(&inst->fields, copyString("_ptr", 4), &v)) return nil;
	if (!IS_NUMBER(v)) return nil;
	return (Dict*)(uintptr)AS_NUMBER(v);
}

static void
dict_collect_entry(char *key, void *value, void *ctx)
{
	if (ctx == nil) return;
	ObjArray* entries = (ObjArray*)ctx;
	ObjInstance* entry = newInstance(nil);
	push(OBJ_VAL(entry));

	tableSet(&entry->fields, copyString("key", 3), OBJ_VAL(copyString(key, (int)strlen(key))));

	if (value != nil) {
		tableSet(&entry->fields, copyString("value", 5),
		         OBJ_VAL(copyString((char*)value, (int)strlen((char*)value))));
	} else {
		tableSet(&entry->fields, copyString("value", 5), NIL_VAL);
	}

	writeArray(entries, OBJ_VAL(entry));
	pop();
}

Value
dictIterNative(int argCount, Value* args)
{
	if (argCount != 1 || !IS_INSTANCE(args[0]))
		return NIL_VAL;
	Dict *d = getDictFromInstance(args[0]);
	if (d == nil) return NIL_VAL;
	ObjArray* entries = newArray();
	push(OBJ_VAL(entries));
	dict_iter(d, dict_collect_entry, entries);
	Value result = OBJ_VAL(entries);
	pop();
	return result;
}

/* Dict class natives for Lux */

Value
dictInitNative(int argCount, Value* args)
{
	if (argCount < 1 || !IS_INSTANCE(args[0]))
		return NIL_VAL;
	ObjInstance* inst = AS_INSTANCE(args[0]);
	Dict *d = dict_new();
	if (d == nil) return NIL_VAL;
	tableSet(&inst->fields, copyString("_ptr", 4), NUMBER_VAL((double)(uintptr)d));
	return args[0];
}

Value
dictPutNative(int argCount, Value* args)
{
	if (argCount != 3 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1]) || !IS_STRING(args[2]))
		return NIL_VAL;
	Dict *d = getDictFromInstance(args[0]);
	if (d == nil) return NIL_VAL;
	dict_put_str(d, AS_CSTRING(args[1]), AS_CSTRING(args[2]));
	return NIL_VAL;
}

Value
dictGetNative(int argCount, Value* args)
{
	if (argCount != 2 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1]))
		return NIL_VAL;
	Dict *d = getDictFromInstance(args[0]);
	if (d == nil) return NIL_VAL;
	char *v = dict_get_str(d, AS_CSTRING(args[1]));
	if (v == nil) return NIL_VAL;
	Value res = OBJ_VAL(copyString(v, (int)strlen(v)));
	return res;
}

Value
dictHasNative(int argCount, Value* args)
{
	if (argCount != 2 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1]))
		return NIL_VAL;
	Dict *d = getDictFromInstance(args[0]);
	if (d == nil) return NIL_VAL;
	return BOOL_VAL(dict_contains(d, AS_CSTRING(args[1])));
}

Value
dictRemoveNative(int argCount, Value* args)
{
	if (argCount != 2 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1]))
		return NIL_VAL;
	Dict *d = getDictFromInstance(args[0]);
	if (d == nil) return NIL_VAL;
	char *v = (char*)dict_remove(d, AS_CSTRING(args[1]));
	if (v == nil) return NIL_VAL;
	Value res = OBJ_VAL(copyString(v, (int)strlen(v)));
	free(v);
	return res;
}

Value
dictSizeNative(int argCount, Value* args)
{
	if (argCount != 1 || !IS_INSTANCE(args[0]))
		return NIL_VAL;
	Dict *d = getDictFromInstance(args[0]);
	if (d == nil) return NIL_VAL;
	return NUMBER_VAL((double)dict_size(d));
}

static void
free_value_cb(char *k, void *v, void *ctx)
{
	USED(ctx);
	USED(k);
	if (v) free(v);
}

Value
dictClearNative(int argCount, Value* args)
{
	if (argCount != 1 || !IS_INSTANCE(args[0]))
		return NIL_VAL;
	Dict *d = getDictFromInstance(args[0]);
	if (d == nil) return NIL_VAL;
	dict_iter(d, free_value_cb, nil);
	dict_clear(d);
	return NIL_VAL;
}

/* Register function */
void
registerDictNatives(void (*defineNative)(const char*, Value (*)(int, Value*)))
{
	defineNative("Dict.init", dictInitNative); /* class method style isn't used; we'll register class below */
	defineNative("dict_put", dictPutNative);
	defineNative("dict_get", dictGetNative);
	defineNative("dict_has", dictHasNative);
	defineNative("dict_remove", dictRemoveNative);
	defineNative("dict_size", dictSizeNative);
	defineNative("dict_clear", dictClearNative);
	defineNative("dict_iter", dictIterNative);
}

