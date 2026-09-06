#ifndef lux_ninep_h
#define lux_ninep_h

enum {
	NINEP_MAXFILES = 32,
	NINEP_MSIZE = 8192,
	NINEP_ERRMAX = 256
};

typedef struct NinePFile NinePFile;
typedef struct NinePOps NinePOps;
typedef struct NinePListener NinePListener;
typedef struct NinePStat NinePStat;
typedef struct NinePList NinePList;
typedef struct NinePClient NinePClient;

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

struct NinePListener {
	int fd;
	int tcp;
	char path[108];
	char adir[40];
};

struct NinePStat {
	char name[256];
	char uid[64];
	char gid[64];
	int isdir;
	ulong mode;
	uvlong length;
};

struct NinePList {
	char **names;
	int count;
};

int ninep_serve(int fd, NinePFile *files, int nfiles, NinePOps *ops);

int ninep_listen(char *addr, NinePListener *lis);
int ninep_accept(NinePListener *lis);
void ninep_unlisten(NinePListener *lis);
int ninep_dial(char *addr);

NinePClient *ninep_client_attach(int fd, char *uname, char *aname);
char *ninep_client_err(NinePClient *c);
int ninep_client_read(NinePClient *c, char *path, char **data, int *len);
int ninep_client_write(NinePClient *c, char *path, char *data, int len);
int ninep_client_ls(NinePClient *c, char *path, NinePList *list);
int ninep_client_stat(NinePClient *c, char *path, NinePStat *st);
void ninep_list_free(NinePList *list);
void ninep_client_close(NinePClient *c);

#endif
