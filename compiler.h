//#ifndef lux_compiler_h
//#define lux_compiler_h

#ifndef _LUX_COMPILER_H_PROTOTYPE_
#define _LUX_COMPILER_H_PROTOTYPE_

#include "common.h"

/* Forward declare Chunk so the compiler doesn't panic on the pointer type */
typedef struct Chunk Chunk;

/* 
 * If 'bool' is causing the 'function not declared' error, 
 * it is because the compiler doesn't recognize the return type. 
 */
int compile(char* source, Chunk* chunk);

#endif