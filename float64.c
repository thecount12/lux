/*
 * float64.c - Float64Array natives
 * Requires: types.h (OBJ_FLOATARRAY, ObjFloatArray), object.h (newFloatArray, IS_FLOATARRAY, AS_FLOATARRAY)
 */
#include "lux.h"
#include "common.h"
#include "value.h"
#include "chunk.h"
#include "object.h"
#include "vm.h"
#include "memory.h"
#include "table.h"

static Value
float64AvailableNative(int argCount, Value* args)
{
	(void)argCount;
	(void)args;
	Value dummy;
	ObjString* key = copyString("float64_new", 11);
	int ok = tableGet(&vm.globals, key, &dummy);
	return BOOL_VAL(ok);
}

static Value
float64NewNative(int argCount, Value* args)
{
	if (argCount != 1 || !IS_NUMBER(args[0])) {
		vm.nativePanic = 1;
		snprint(vm.nativePanicMsg, sizeof(vm.nativePanicMsg),
		        "float64_new(length) expects 1 numeric argument.");
		return NIL_VAL;
	}
	int len = (int)AS_NUMBER(args[0]);
	if (len < 0) {
		vm.nativePanic = 1;
		snprint(vm.nativePanicMsg, sizeof(vm.nativePanicMsg),
		        "float64_new(length) length must be non-negative.");
		return NIL_VAL;
	}
	ObjFloatArray* fa = newFloatArray(len);
	return OBJ_VAL(fa);
}

static Value
float64GetNative(int argCount, Value* args)
{
	if (argCount != 2 || !IS_OBJ(args[0]) || !IS_FLOATARRAY(args[0]) || !IS_NUMBER(args[1])) {
		vm.nativePanic = 1;
		snprint(vm.nativePanicMsg, sizeof(vm.nativePanicMsg),
		        "float64_get(array, index) expects (Float64Array, number).");
		return NIL_VAL;
	}
	ObjFloatArray* fa = AS_FLOATARRAY(args[0]);
	int idx = (int)AS_NUMBER(args[1]);
	if (idx < 0 || idx >= fa->length) {
		vm.nativePanic = 1;
		snprint(vm.nativePanicMsg, sizeof(vm.nativePanicMsg),
		        "Index out of bounds.");
		return NIL_VAL;
	}
	return NUMBER_VAL(fa->elems[idx]);
}

static Value
float64SetNative(int argCount, Value* args)
{
	if (argCount != 3 || !IS_OBJ(args[0]) || !IS_FLOATARRAY(args[0]) || !IS_NUMBER(args[1]) || !IS_NUMBER(args[2])) {
		vm.nativePanic = 1;
		snprint(vm.nativePanicMsg, sizeof(vm.nativePanicMsg),
		        "float64_set(array, index, value) expects (Float64Array, number, number).");
		return NIL_VAL;
	}
	ObjFloatArray* fa = AS_FLOATARRAY(args[0]);
	int idx = (int)AS_NUMBER(args[1]);
	if (idx < 0 || idx >= fa->length) {
		vm.nativePanic = 1;
		snprint(vm.nativePanicMsg, sizeof(vm.nativePanicMsg),
		        "Index out of bounds.");
		return NIL_VAL;
	}
	fa->elems[idx] = AS_NUMBER(args[2]);
	return BOOL_VAL(1);
}

static Value
float64DotNative(int argCount, Value* args)
{
	if (argCount != 2 || !IS_OBJ(args[0]) || !IS_FLOATARRAY(args[0]) || !IS_OBJ(args[1]) || !IS_FLOATARRAY(args[1])) {
		vm.nativePanic = 1;
		snprint(vm.nativePanicMsg, sizeof(vm.nativePanicMsg),
		        "float64_dot(a, b) expects two Float64Array arguments.");
		return NIL_VAL;
	}
	ObjFloatArray* a = AS_FLOATARRAY(args[0]);
	ObjFloatArray* b = AS_FLOATARRAY(args[1]);
	if (a->length != b->length) {
		vm.nativePanic = 1;
		snprint(vm.nativePanicMsg, sizeof(vm.nativePanicMsg),
		        "float64_dot: arrays must have same length.");
		return NIL_VAL;
	}
	double sum = 0.0;
	for (int i = 0; i < a->length; i++) sum += a->elems[i] * b->elems[i];
	return NUMBER_VAL(sum);
}

void
registerFloat64Natives(void (*defineNative)(const char*, Value (*)(int, Value*)))
{
	defineNative("float64_available", float64AvailableNative);
	defineNative("float64_new", float64NewNative);
	defineNative("float64_get", float64GetNative);
	defineNative("float64_set", float64SetNative);
	defineNative("float64_dot", float64DotNative);
}
