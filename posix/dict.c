#include <stdlib.h>
#include <string.h>

#include "dict.h"

enum {
	INITIAL_CAPACITY = 16,
	LOAD_NUM = 3, /* load factor = LOAD_NUM / LOAD_DEN */
	LOAD_DEN = 4
};

static unsigned long
hashstr(const char *s)
{
	unsigned long h = 2166136261UL;
	const unsigned char *p = (const unsigned char*)s;
	while (*p) {
		h ^= *p++;
		h *= 16777619;
	}
	return h;
}

static char*
xstrdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *p = malloc(n);
	if (p == NULL) return NULL;
	memcpy(p, s, n);
	return p;
}

static DictEntry**
alloc_buckets(int n)
{
	DictEntry **b = malloc(n * sizeof(DictEntry*));
	if (b == NULL) return NULL;
	memset(b, 0, n * sizeof(DictEntry*));
	return b;
}

static void
dict_rehash(Dict *d, int newcap)
{
	DictEntry **old = d->buckets;
	int oldcap = d->capacity;

	DictEntry **b = alloc_buckets(newcap);
	if (b == NULL) return; /* OOM: keep old table */

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
	free(old);
}

Dict*
dict_new(void)
{
	Dict *d = malloc(sizeof(Dict));
	if (d == NULL) return NULL;
	d->capacity = INITIAL_CAPACITY;
	d->count = 0;
	d->buckets = alloc_buckets(d->capacity);
	if (d->buckets == NULL) {
		free(d);
		return NULL;
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
	return dict_get(d, key) != NULL;
}

void*
dict_get(Dict *d, char *key)
{
	if (d == NULL) return NULL;
	unsigned long h = hashstr(key);
	int idx = h % d->capacity;
	for (DictEntry *e = d->buckets[idx]; e; e = e->next) {
		if (strcmp(e->key, key) == 0)
			return e->value;
	}
	return NULL;
}

void
dict_put(Dict *d, char *key, void *value)
{
	if (d == NULL) return;
	if ((d->count + 1) * LOAD_DEN > d->capacity * LOAD_NUM)
		dict_rehash(d, d->capacity * 2);

	unsigned long h = hashstr(key);
	int idx = h % d->capacity;
	for (DictEntry *e = d->buckets[idx]; e; e = e->next) {
		if (strcmp(e->key, key) == 0) {
			e->value = value;
			return;
		}
	}
	DictEntry *ne = malloc(sizeof(DictEntry));
	if (ne == NULL) return;
	ne->key = xstrdup(key);
	if (ne->key == NULL) { free(ne); return; }
	ne->value = value;
	ne->next = d->buckets[idx];
	d->buckets[idx] = ne;
	d->count++;
}

void*
dict_remove(Dict *d, char *key)
{
	if (d == NULL) return NULL;
	unsigned long h = hashstr(key);
	int idx = h % d->capacity;
	DictEntry *prev = NULL;
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
	return NULL;
}

void
dict_clear(Dict *d)
{
	if (d == NULL) return;
	for (int i = 0; i < d->capacity; i++) {
		DictEntry *e = d->buckets[i];
		while (e) {
			DictEntry *next = e->next;
			free(e->key);
			free(e);
			e = next;
		}
		d->buckets[i] = NULL;
	}
	d->count = 0;
}

void
dict_free(Dict *d)
{
	if (d == NULL) return;
	dict_clear(d);
	free(d->buckets);
	free(d);
}

void
dict_iter(Dict *d, void (*fn)(char*, void*, void*), void *ctx)
{
	if (d == NULL || fn == NULL) return;
	for (int i = 0; i < d->capacity; i++) {
		for (DictEntry *e = d->buckets[i]; e; e = e->next) {
			fn(e->key, e->value, ctx);
		}
	}
}

void
dict_put_str(Dict *d, char *key, char *value)
{
	if (d == NULL) return;
	if ((d->count + 1) * LOAD_DEN > d->capacity * LOAD_NUM)
		dict_rehash(d, d->capacity * 2);

	unsigned long h = hashstr(key);
	int idx = h % d->capacity;
	for (DictEntry *e = d->buckets[idx]; e; e = e->next) {
		if (strcmp(e->key, key) == 0) {
			if (e->value != NULL) free(e->value);
			e->value = xstrdup(value);
			return;
		}
	}
	DictEntry *ne = malloc(sizeof(DictEntry));
	if (ne == NULL) return;
	ne->key = xstrdup(key);
	if (ne->key == NULL) { free(ne); return; }
	ne->value = xstrdup(value);
	ne->next = d->buckets[idx];
	d->buckets[idx] = ne;
	d->count++;
}

char*
dict_get_str(Dict *d, char *key)
{
	void *v = dict_get(d, key);
	return v ? (char*)v : NULL;
}
