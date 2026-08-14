#ifndef lux_ninep_h
#define lux_ninep_h

enum {
	NINEP_MAXFILES = 32,
	NINEP_MSIZE = 8192
};

typedef struct NinePFile NinePFile;
typedef struct NinePOps NinePOps;

struct NinePFile {
	char name[64];
	int readable;
	int writable;
	void *aux;
};

struct NinePOps {
	int (*onread)(void *aux, char **data, int *len);
	int (*onwrite)(void *aux, char *data, int len);
};

int ninep_serve(int fd, NinePFile *files, int nfiles, NinePOps *ops);

#endif
