
/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

#include <sophia.h>
#include "test.h"

static char *dbrep = "./rep";

static inline int
cmp(char *a, size_t asz, char *b, size_t bsz, void *arg) {
	register uint32_t av = *(uint32_t*)a;
	register uint32_t bv = *(uint32_t*)b;
	if (av == bv)
		return 0;
	return (av > bv) ? 1 : -1;
}

static void
limit_key(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	char buf[1];
	t( sp_set(db, buf, UINT16_MAX + 1, buf, sizeof(buf)) == -1 );
	t( sp_error(env) != NULL );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	rmrf(dbrep);
}

static void
limit_value(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	char buf[1];
	t( sp_set(db, buf, sizeof(buf), buf, UINT32_MAX + 1ULL) == -1 );
	t( sp_error(env) != NULL );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	rmrf(dbrep);
}

int
main(int argc, char *argv[])
{
	rmrf(dbrep);

	test(limit_key);
	if (sizeof(size_t) > 4)
		test(limit_value);
	return 0;
}
