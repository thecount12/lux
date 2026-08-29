#ifndef lux_file_native_h
#define lux_file_native_h

Value fileInitNative(int argCount, Value* args);
Value fileReadLineNative(int argCount, Value* args);
Value fileCloseNative(int argCount, Value* args);
void fileSetClass(ObjClass* klass);
void fileCloseFromInstance(ObjInstance* instance);

#endif
