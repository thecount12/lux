#include "lux.h"
#include "dict.h"

enum {
	INITIAL_CAPACITY = 16,
	LOAD_NUM = 3, /* load factor = LOAD_NUM / LOAD_DEN */
	LOAD_DEN = 4
};

static unsigned long
hashstr(char *s)
{
	unsigned long h = 2166136261UL;
	unsigned char *p = (unsigned char*)s;
	while (*p) {
		h ^= *p++;
		h *= 16777619;
	}
	return h;
}

static char*
xstrdup(char *s)
{
	int n = strlen(s) + 1;
	char *p = malloc(n);
	if (p == nil) return nil;
	memcpy(p, s, n);
	return p;
}

static DictEntry**
alloc_buckets(int n)
{
	DictEntry **b = malloc(n * sizeof(DictEntry*));
	if (b == nil) return nil;
	memset(b, 0, n * sizeof(DictEntry*));
	return b;
}

static void
dict_rehash(Dict *d, int newcap)
{
	DictEntry **old = d->buckets;
	int oldcap = d->capacity;

	DictEntry **b = alloc_buckets(newcap);
	if (b == nil) return; /* OOM: keep old table */

	d->buckets = b;
	d->capacity = newcap;
	d->count = 0;

	for (int i = 0; i < oldcap; i++) {
		DictEntry *e = old[i];
		while (e) {
			dict_put(d, e->key, e->value);
			DictEntry *next = e->next;
			free(e->key);
			free(e);
			e = next;
		}
	}
}

Dict*
dict_new(void)
{
	Dict *d = malloc(sizeof(Dict));
	if (d == nil) return nil;
	d->capacity = INITIAL_CAPACITY;
	d->count = 0;
	d->buckets = alloc_buckets(d->capacity);
	if (d->buckets == nil) {
		free(d);
		return nil;
	}
	return d;
}

int
dict_size(Dict *d)
{
	return d->count;
}

int
dict_contains(Dict *d, char *key)
{
	return dict_get(d, key) != nil;
}

void*
dict_get(Dict *d, char *key)
{
	if (d == nil) return nil;
	unsigned long h = hashstr(key);
	int idx = h % d->capacity;
	for (DictEntry *e = d->buckets[idx]; e; e = e->next) {
		if (strcmp(e->key, key) == 0)
			return e->value;
	}
	return nil;
}

void
dict_put(Dict *d, char *key, void *value)
{
	if (d == nil) return;
	/* resize if load too high */
	if ((d->count + 1) * LOAD_DEN > d->capacity * LOAD_NUM)
		dict_rehash(d, d->capacity * 2);

	unsigned long h = hashstr(key);
	int idx = h % d->capacity;
	for (DictEntry *e = d->buckets[idx]; e; e = e->next) {
		if (strcmp(e->key, key) == 0) {
			/* overwrite value; caller owns previous value */
			e->value = value;
			return;
		}
	}
	DictEntry *ne = malloc(sizeof(DictEntry));
	if (ne == nil) return;
	ne->key = xstrdup(key);
	if (ne->key == nil) { free(ne); return; }
	ne->value = value;
	ne->next = d->buckets[idx];
	d->buckets[idx] = ne;
	d->count++;
}

void*
dict_remove(Dict *d, char *key)
{
	if (d == nil) return nil;
	unsigned long h = hashstr(key);
	int idx = h % d->capacity;
	DictEntry *prev = nil;
	for (DictEntry *e = d->buckets[idx]; e; e = e->next) {
		if (strcmp(e->key, key) == 0) {
			void *val = e->value;
			if (prev)
				prev->next = e->next;
			else
				d->buckets[idx] = e->next;
			free(e->key);
			free(e);
			d->count--;
			return val;
		}
		prev = e;
	}
	return nil;
}

void
dict_clear(Dict *d)
{
	if (d == nil) return;
	for (int i = 0; i < d->capacity; i++) {
		DictEntry *e = d->buckets[i];
		while (e) {
			DictEntry *next = e->next;
			free(e->key);
			free(e);
			e = next;
		}
		d->buckets[i] = nil;
	}
	d->count = 0;
}

void
dict_free(Dict *d)
{
	if (d == nil) return;
	dict_clear(d);
	free(d->buckets);
	free(d);
}

void
dict_iter(Dict *d, void (*fn)(char*, void*, void*), void *ctx)
{
	if (d == nil || fn == nil) return;
	for (int i = 0; i < d->capacity; i++) {
		for (DictEntry *e = d->buckets[i]; e; e = e->next) {
			fn(e->key, e->value, ctx);
		}
	}
}

/* String-specific helpers that duplicate the provided value string.
 * The stored value memory is owned by the dictionary and will be freed
 * when entries are removed or when dict_clear/dict_free are called.
 */
void
dict_put_str(Dict *d, char *key, char *value)
{
	if (d == nil) return;
	/* resize if load too high */
	if ((d->count + 1) * LOAD_DEN > d->capacity * LOAD_NUM)
		dict_rehash(d, d->capacity * 2);

	unsigned long h = hashstr(key);
	int idx = h % d->capacity;
	for (DictEntry *e = d->buckets[idx]; e; e = e->next) {
		if (strcmp(e->key, key) == 0) {
			/* overwrite value: free previous if present */
			if (e->value != nil) free(e->value);
			e->value = xstrdup(value);
			return;
		}
	}
	DictEntry *ne = malloc(sizeof(DictEntry));
	if (ne == nil) return;
	ne->key = xstrdup(key);
	if (ne->key == nil) { free(ne); return; }
	ne->value = xstrdup(value);
	ne->next = d->buckets[idx];
	d->buckets[idx] = ne;
	d->count++;
}

char*
dict_get_str(Dict *d, char *key)
{
	void *v = dict_get(d, key);
	return v ? (char*)v : nil;
}

