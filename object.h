#ifndef lux_object_h
#define lux_object_h

/* object.h - function declarations and macros
 * Assumes: types.h has been included by the .c file
 */

#define OBJ_TYPE(value)     (AS_OBJ(value)->type)
#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define IS_NATIVE(value)	isObjType(value, OBJ_NATIVE)
#define AS_NATIVE(value) \
	(((struct ObjNative*)AS_OBJ(value))->function)
#define AS_CSTRING(value)	(((ObjString*)AS_OBJ(value))->chars)

// Fix macro typo: change ObjTypeFunction to ObjFunction
#define AS_FUNCTION(value)  	((ObjFunction*)AS_OBJ(value))
ObjFunction* newFunction(void);
ObjNative* newNative(NativeFn function);
ObjString* takeString(char* chars, int length);
ObjString* copyString(const char* chars, int length);
void printObject(Value value);

static bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif
