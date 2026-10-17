#ifndef lux_tensor_native_h
#define lux_tensor_native_h

#include "object.h"
#include "value.h"

Value tensorInitNative(int argCount, Value* args);
Value tensorFillNative(int argCount, Value* args);
Value tensorSetNative(int argCount, Value* args);
Value tensorGetNative(int argCount, Value* args);
Value tensorMatmulNative(int argCount, Value* args);
Value tensorAddBiasNative(int argCount, Value* args);
Value tensorReluNative(int argCount, Value* args);
Value tensorShowNative(int argCount, Value* args);
void tensorSetClass(ObjClass* klass);
void tensorFreeFromInstance(ObjInstance* instance);

#endif
