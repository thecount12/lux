#ifndef lux_posix_dict_h
#define lux_posix_dict_h

typedef struct DictEntry DictEntry;
typedef struct Dict Dict;

struct DictEntry {
	char *key;
	void *value;
	DictEntry *next;
};

struct Dict {
	DictEntry **buckets;
	int capacity;
	int count;
};

Dict*	dict_new(void);
void	dict_free(Dict*);
int	dict_size(Dict*);
int	dict_contains(Dict*, char*);
void*	dict_get(Dict*, char*);
void	dict_put(Dict*, char*, void*);
void*	dict_remove(Dict*, char*);
void	dict_clear(Dict*);
void	dict_iter(Dict*, void (*)(char*, void*, void*), void*);

void	dict_put_str(Dict*, char*, char*);
char*	dict_get_str(Dict*, char*);

#endif
