#ifndef lux_ninep_h
#define lux_ninep_h

#define NINEP_MAXFILES 32
#define NINEP_MSIZE 8192

typedef struct NinePFile {
	char name[64];
	int readable;
	int writable;
	void* aux;
} NinePFile;

typedef struct NinePOps {
	int (*onread)(void* aux, char** data, int* len);
	int (*onwrite)(void* aux, const char* data, int len);
} NinePOps;

int ninep_serve(int fd, NinePFile* files, int nfiles, NinePOps* ops);

#endif
