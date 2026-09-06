#ifndef lux_ninep_h
#define lux_ninep_h

#define NINEP_MAXFILES 32
#define NINEP_MSIZE 8192
#define NINEP_ERRMAX 256

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

typedef struct NinePListener {
	int fd;
	int tcp;
	char path[108];
} NinePListener;

typedef struct NinePStat {
	char name[256];
	char uid[64];
	char gid[64];
	int isdir;
	unsigned int mode;
	unsigned long long length;
} NinePStat;

typedef struct NinePList {
	char** names;
	int count;
} NinePList;

typedef struct NinePClient NinePClient;

int ninep_serve(int fd, NinePFile* files, int nfiles, NinePOps* ops);

int ninep_listen(const char* addr, NinePListener* lis);
int ninep_accept(NinePListener* lis);
void ninep_unlisten(NinePListener* lis);
int ninep_bound_port(NinePListener* lis);
int ninep_dial(const char* addr);

NinePClient* ninep_client_attach(int fd, const char* uname, const char* aname);
const char* ninep_client_err(NinePClient* c);
int ninep_client_read(NinePClient* c, const char* path, char** data, int* len);
int ninep_client_write(NinePClient* c, const char* path, const char* data, int len);
int ninep_client_ls(NinePClient* c, const char* path, NinePList* list);
int ninep_client_stat(NinePClient* c, const char* path, NinePStat* st);
void ninep_list_free(NinePList* list);
void ninep_client_close(NinePClient* c);

#endif
