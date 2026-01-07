#ifndef _LUX_COMPILER_H_
#define _LUX_COMPILER_H_

#include "common.h"

/* Use the tag exclusively for the forward declaration */
struct Chunk; 

/* Use 'struct Chunk' in the prototype to match the tag namespace */
int compile(char* source, struct Chunk* chunk);

#endif