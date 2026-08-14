#include "lux.h"
#include "common.h"
#include "value.h"
#include "object.h"
#include "vm.h"
#include "table.h"
#include "luxdraw.h"

Value
plumbNative(int argCount, Value *args)
{
	char *dst, *data, *wdir;

	if(argCount < 2 || argCount > 3 || !IS_STRING(args[0]) || !IS_STRING(args[1]))
		return BOOL_VAL(false);
	dst = AS_CSTRING(args[0]);
	data = AS_CSTRING(args[1]);
	wdir = ".";
	if(argCount == 3){
		if(!IS_STRING(args[2]))
			return BOOL_VAL(false);
		wdir = AS_CSTRING(args[2]);
	}
	if(luxplumb_send(dst, data, wdir) < 0)
		return BOOL_VAL(false);
	return BOOL_VAL(true);
}

Value
plumbRecvNative(int argCount, Value *args)
{
	LuxPlumbMsg msg;
	ObjInstance *inst;

	USED(args);
	if(argCount != 0)
		return NIL_VAL;
	if(luxplumb_recv(&msg) < 0)
		return NIL_VAL;
	inst = newInstance(nil);
	push(OBJ_VAL(inst));
	tableSet(&inst->fields, copyString("src", 3),
	         OBJ_VAL(copyString(msg.src ? msg.src : "", msg.src ? strlen(msg.src) : 0)));
	tableSet(&inst->fields, copyString("dst", 3),
	         OBJ_VAL(copyString(msg.dst ? msg.dst : "", msg.dst ? strlen(msg.dst) : 0)));
	tableSet(&inst->fields, copyString("wdir", 4),
	         OBJ_VAL(copyString(msg.wdir ? msg.wdir : "", msg.wdir ? strlen(msg.wdir) : 0)));
	tableSet(&inst->fields, copyString("type", 4),
	         OBJ_VAL(copyString(msg.type ? msg.type : "", msg.type ? strlen(msg.type) : 0)));
	tableSet(&inst->fields, copyString("data", 4),
	         OBJ_VAL(copyString(msg.data ? msg.data : "", msg.data ? strlen(msg.data) : 0)));
	luxplumb_msg_free(&msg);
	pop();
	return OBJ_VAL(inst);
}
