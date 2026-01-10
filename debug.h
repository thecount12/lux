#ifndef lux_debug_h
#define lux_debug_h

void 
disassembleChunk(Chunk* chunk, char* name);

int 
disassembleInstruction(Chunk* chunk, int offset);

#endif
