#ifndef lux_ninep_native_h
#define lux_ninep_native_h

#include "object.h"
#include "value.h"

Value ninepInitNative(int argCount, Value* args);
Value ninepFileNative(int argCount, Value* args);
Value ninepExportNative(int argCount, Value* args);
Value ninepListenNative(int argCount, Value* args);
Value ninepPostNative(int argCount, Value* args);
Value ninepConnectNative(int argCount, Value* args);
Value ninepReadNative(int argCount, Value* args);
Value ninepWriteNative(int argCount, Value* args);
Value ninepGetNative(int argCount, Value* args);
Value ninepPutNative(int argCount, Value* args);
Value ninepLsNative(int argCount, Value* args);
Value ninepStatNative(int argCount, Value* args);
Value ninepCloseNative(int argCount, Value* args);
void ninepSetClasses(ObjClass* server, ObjClass* conn);
void ninepCloseFromInstance(ObjInstance* instance);

#endif
