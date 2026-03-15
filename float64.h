#ifndef lux_float64_h
#define lux_float64_h

#include "common.h"

/* Register Float64Array natives with the VM.
 * Pass the VM's defineNative function.
 */
void registerFloat64Natives(void (*defineNative)(const char*, Value (*)(int, Value*)));

#endif
