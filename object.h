#ifndef lux_object_h
#define lux_object_h

/* object.h - function declarations and macros
 * Assumes: types.h has been included by the .c file
 */

#define OBJ_TYPE(value)     (AS_OBJ(value)->type)
#define IS_ARRAY(value)    isObjType(value, OBJ_ARRAY)
#define IS_FLOATARRAY(value) isObjType(value, OBJ_FLOATARRAY)
#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define IS_INSTANCE(value) isObjType(value, OBJ_INSTANCE)
#define IS_NATIVE(value)	isObjType(value, OBJ_NATIVE)

#define IS_BOUND_METHOD(value) isObjType(value, OBJ_BOUND_METHOD)
#define IS_CLASS(value)		isObjType(value, OBJ_CLASS)
#define IS_CLOSURE(value)	isObjType(value, OBJ_CLOSURE)

#define AS_NATIVE(value) \
	(((struct ObjNative*)AS_OBJ(value))->function)
#define AS_ARRAY(value)     ((ObjArray*)AS_OBJ(value))
#define AS_FLOATARRAY(value) ((ObjFloatArray*)AS_OBJ(value))
#define AS_CSTRING(value)	(((ObjString*)AS_OBJ(value))->chars)

// Fix macro typo: change ObjTypeFunction to ObjFunction
#define AS_FUNCTION(value)  	((ObjFunction*)AS_OBJ(value))
#define AS_INSTANCE(value)  ((ObjInstance*)AS_OBJ(value))

#define AS_BOUND_METHOD(value) ((ObjBoundMethod*)AS_OBJ(value))
#define AS_CLASS(value)		((ObjClass*)AS_OBJ(value))
#define AS_CLOSURE(value) 	((ObjClosure*)AS_OBJ(value))

ObjBoundMethod* newBoundMethod(Value receiver, ObjClosure* method);
ObjFunction* newFunction(void);
ObjClass* newClass(ObjString* name);
ObjInstance* newInstance(ObjClass* klass);
ObjClosure* newClosure(ObjFunction* function);
ObjNative* newNative(NativeFn function);
ObjArray* newArray(void);
ObjFloatArray* newFloatArray(int length);
void writeArray(ObjArray* array, Value value);
ObjString* takeString(char* chars, int length);
ObjString* copyString(const char* chars, int length);
ObjString* valueToString(Value value);
ObjUpvalue* newUpvalue(Value* slot);
void printObject(Value value);

/* posix only
static bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}
*/

static bool 
isObjType(Value value, ObjType type) 
{
	int isObj, typeMatches;
	isObj = IS_OBJ(value);
	if (!isObj) return false;
	typeMatches = (AS_OBJ(value)->type == type);
	return typeMatches;
}

#endif
