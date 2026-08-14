#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "common.h"
#include "value.h"
#include "object.h"
#include "vm.h"
#include "table.h"
#include "luxdraw.h"

static int
parseColor(const char* s, int* r, int* g, int* b)
{
	unsigned int rv, gv, bv;
	if (s == NULL || s[0] != '#' || strlen(s) != 7)
		return 0;
	if (sscanf(s + 1, "%02x%02x%02x", &rv, &gv, &bv) != 3)
		return 0;
	*r = (int)rv;
	*g = (int)gv;
	*b = (int)bv;
	return 1;
}

static int
drawHandle(Value instVal)
{
	ObjInstance* inst;
	Value v;
	if (!IS_INSTANCE(instVal))
		return 0;
	inst = AS_INSTANCE(instVal);
	if (!tableGet(&inst->fields, copyString("_ptr", 4), &v))
		return 0;
	if (!IS_NUMBER(v))
		return 0;
	return (int)AS_NUMBER(v);
}

Value
drawAvailableNative(int argCount, Value* args)
{
	(void)argCount;
	(void)args;
	return BOOL_VAL(luxdraw_available() ? true : false);
}

Value
snarfGetNative(int argCount, Value* args)
{
	char* s;
	int n;
	Value out;
	(void)args;
	if (argCount != 0)
		return NIL_VAL;
	if (luxsnarf_get(&s, &n) < 0)
		return NIL_VAL;
	out = OBJ_VAL(copyString(s, n));
	free(s);
	return out;
}

Value
snarfPutNative(int argCount, Value* args)
{
	if (argCount != 1 || !IS_STRING(args[0]))
		return BOOL_VAL(false);
	if (luxsnarf_put(AS_CSTRING(args[0]), AS_STRING(args[0])->length) < 0)
		return BOOL_VAL(false);
	return BOOL_VAL(true);
}

Value
drawInitNative(int argCount, Value* args)
{
	ObjInstance* inst;
	const char* title;
	int w, h;

	if (argCount != 4 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1]) ||
	    !IS_NUMBER(args[2]) || !IS_NUMBER(args[3]))
		return NIL_VAL;
	if (luxdraw_is_open())
		return NIL_VAL;
	title = AS_CSTRING(args[1]);
	w = (int)AS_NUMBER(args[2]);
	h = (int)AS_NUMBER(args[3]);
	if (w <= 0 || h <= 0)
		return NIL_VAL;
	if (luxdraw_open(title, w, h) < 0)
		return NIL_VAL;
	inst = AS_INSTANCE(args[0]);
	tableSet(&inst->fields, copyString("_ptr", 4), NUMBER_VAL(1));
	return args[0];
}

Value
drawFillNative(int argCount, Value* args)
{
	int r, g, b;
	if (argCount != 6 || drawHandle(args[0]) == 0)
		return BOOL_VAL(false);
	if (!IS_NUMBER(args[1]) || !IS_NUMBER(args[2]) || !IS_NUMBER(args[3]) ||
	    !IS_NUMBER(args[4]) || !IS_STRING(args[5]))
		return BOOL_VAL(false);
	if (!parseColor(AS_CSTRING(args[5]), &r, &g, &b))
		return BOOL_VAL(false);
	if (luxdraw_fill((int)AS_NUMBER(args[1]), (int)AS_NUMBER(args[2]),
	                 (int)AS_NUMBER(args[3]), (int)AS_NUMBER(args[4]), r, g, b) < 0)
		return BOOL_VAL(false);
	return BOOL_VAL(true);
}

Value
drawStringNative(int argCount, Value* args)
{
	int r, g, b;
	if (argCount != 5 || drawHandle(args[0]) == 0)
		return BOOL_VAL(false);
	if (!IS_NUMBER(args[1]) || !IS_NUMBER(args[2]) || !IS_STRING(args[3]) ||
	    !IS_STRING(args[4]))
		return BOOL_VAL(false);
	if (!parseColor(AS_CSTRING(args[4]), &r, &g, &b))
		return BOOL_VAL(false);
	if (luxdraw_string((int)AS_NUMBER(args[1]), (int)AS_NUMBER(args[2]),
	                   AS_CSTRING(args[3]), r, g, b) < 0)
		return BOOL_VAL(false);
	return BOOL_VAL(true);
}

Value
drawFlushNative(int argCount, Value* args)
{
	if (argCount != 1 || drawHandle(args[0]) == 0)
		return BOOL_VAL(false);
	if (luxdraw_flush() < 0)
		return BOOL_VAL(false);
	return BOOL_VAL(true);
}

Value
drawEventNative(int argCount, Value* args)
{
	LuxDrawEvent ev;
	ObjInstance* inst;
	const char* kind;

	if (argCount != 1 || drawHandle(args[0]) == 0)
		return NIL_VAL;
	if (luxdraw_event(&ev) < 0)
		return NIL_VAL;
	inst = newInstance(NULL);
	push(OBJ_VAL(inst));
	switch (ev.kind) {
	case 0: kind = "mouse"; break;
	case 1: kind = "kbd"; break;
	case 2: kind = "resize"; break;
	default: kind = "quit"; break;
	}
	tableSet(&inst->fields, copyString("kind", 4),
	         OBJ_VAL(copyString(kind, (int)strlen(kind))));
	tableSet(&inst->fields, copyString("x", 1), NUMBER_VAL((double)ev.x));
	tableSet(&inst->fields, copyString("y", 1), NUMBER_VAL((double)ev.y));
	tableSet(&inst->fields, copyString("button", 6), NUMBER_VAL((double)ev.button));
	tableSet(&inst->fields, copyString("r", 1), NUMBER_VAL((double)ev.r));
	pop();
	return OBJ_VAL(inst);
}

Value
drawCloseNative(int argCount, Value* args)
{
	ObjInstance* inst;
	if (argCount != 1 || !IS_INSTANCE(args[0]))
		return BOOL_VAL(false);
	inst = AS_INSTANCE(args[0]);
	luxdraw_close();
	tableSet(&inst->fields, copyString("_ptr", 4), NUMBER_VAL(0));
	return BOOL_VAL(true);
}
