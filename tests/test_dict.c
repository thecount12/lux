/*
8c -FTVw dict.c
8c -FTVw tests/test_dict.c
8l -o testdict tests/test_dict.8 dict.8

*/
#include <u.h>
#include <libc.h>
#include "../dict.h"

static void
collect_print(char *k, void *v, void *ctx)
{
	(void)ctx;
	print("iter: %s -> %s\n", k, (char*)v);
}

void
main(void)
{
	Dict *d = dict_new();
	if (d == nil) {
		fprint(2, "dict_new failed\n");
		exits("fail");
	}

	/* insert and get */
	char *v1 = "value1";
	dict_put(d, "key1", v1);
	if (dict_get(d, "key1") != v1) {
		fprint(2, "get after put failed\n");
		exits("fail");
	}

	/* overwrite */
	char *v2 = "value2";
	dict_put(d, "key1", v2);
	if (dict_get(d, "key1") != v2) {
		fprint(2, "overwrite failed\n");
		exits("fail");
	}

	/* size and contains */
	dict_put(d, "k2", "v");
	if (dict_size(d) != 2) {
		fprint(2, "size expected 2, got %d\n", dict_size(d));
		exits("fail");
	}
	if (!dict_contains(d, "k2")) {
		fprint(2, "contains k2 failed\n");
		exits("fail");
	}

	/* remove */
	void *r = dict_remove(d, "k2");
	if (r == nil) {
		fprint(2, "remove k2 returned nil\n");
		exits("fail");
	}
	if (dict_contains(d, "k2")) {
		fprint(2, "k2 still present after remove\n");
		exits("fail");
	}

	/* iterate (just ensure it runs) */
	dict_iter(d, collect_print, nil);

	dict_free(d);
	print("dict tests passed\n");
	exits(nil);
}

