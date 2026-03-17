/* Simple chaining hash table (Plan 9 style)
 *
 * - string keys are copied by the dictionary
 * - values are stored as void* and ownership remains with caller
 * - duplicate keys overwrite existing value
 * - dynamically resizes
 */
#ifndef lux_dict_h
#define lux_dict_h

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

/* String-specific helpers (duplicate stored value) */
void	dict_put_str(Dict*, char*, char*);
char*	dict_get_str(Dict*, char*);

#endif

