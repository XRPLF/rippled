
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
recover_log_set_get(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 1;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
	k = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
	k = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	size_t vsize = 0;
	void *vp = NULL;
	k = 1;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 1 );
	t( vsize == sizeof(v) );
	t( *(uint32_t*)vp == v );
	free(vp);
	k = 2;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 1 );
	t( vsize == sizeof(v) );
	t( *(uint32_t*)vp == v );
	free(vp);
	k = 3;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 1 );
	t( vsize == sizeof(v) );
	t( *(uint32_t*)vp == v );
	free(vp);
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
recover_log_replace_get(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 1;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
	v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
	v = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	size_t vsize = 0;
	void *vp = NULL;
	k = 1;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 1 );
	t( vsize == sizeof(v) );
	t( *(uint32_t*)vp == 3 );
	free(vp);
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
recover_log_set_get_replace_get(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 1;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	size_t vsize = 0;
	void *vp = NULL;
	k = 1;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 1 );
	t( vsize == sizeof(v) );
	t( *(uint32_t*)vp == 1 );
	free(vp);
	v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
	vsize = 0;
	vp = NULL;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 1 );
	t( vsize == sizeof(v) );
	t( *(uint32_t*)vp == 2 );
	free(vp);
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	vsize = 0;
	vp = NULL;
	k = 1;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 1 );
	t( vsize == sizeof(v) );
	t( *(uint32_t*)vp == 2 );
	free(vp);
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
recover_log_delete_get(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 1;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_delete(db, &k, sizeof(k)) == 0 );
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	size_t vsize = 0;
	void *vp = NULL;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
recover_log_delete_set_get(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 1;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_delete(db, &k, sizeof(k)) == 0 );
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	size_t vsize = 0;
	void *vp = NULL;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 0 );
	v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	vsize = 0;
	vp = NULL;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 1 );
	t( vsize == sizeof(v) );
	t( *(uint32_t*)vp == 2 );
	free(vp);
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
recover_log_fetch_gte(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	void *cur = sp_cursor(db, SPGTE, NULL, 0);
	t( cur != NULL );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 1 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 2 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 3 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 0 );
	t( sp_fetch(cur) == 0 );
	t( sp_destroy(cur) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
recover_log_fetch_lte(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	void *cur = sp_cursor(db, SPLTE, NULL, 0);
	t( cur != NULL );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 3 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 2 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 1 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 0 );
	t( sp_fetch(cur) == 0 );
	t( sp_destroy(cur) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
recover_log_fetch_kgte(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	k = 2;
	void *cur = sp_cursor(db, SPGTE, &k, sizeof(k));
	t( cur != NULL );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 2 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 3 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 0 );
	t( sp_fetch(cur) == 0 );
	t( sp_destroy(cur) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
recover_log_fetch_kgt(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	k = 2;
	void *cur = sp_cursor(db, SPGT, &k, sizeof(k));
	t( cur != NULL );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 3 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 0 );
	t( sp_fetch(cur) == 0 );
	t( sp_destroy(cur) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
recover_log_fetch_klte(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	k = 2;
	void *cur = sp_cursor(db, SPLTE, &k, sizeof(k));
	t( cur != NULL );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 2 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 1 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 0 );
	t( sp_fetch(cur) == 0 );
	t( sp_destroy(cur) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
recover_log_fetch_klt(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	k = 2;
	void *cur = sp_cursor(db, SPLT, &k, sizeof(k));
	t( cur != NULL );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 1 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 0 );
	t( sp_keysize(cur) == 0 );
	t( sp_key(cur) == NULL );
	t( sp_valuesize(cur) == 0 );
	t( sp_value(cur) == NULL );
	t( sp_fetch(cur) == 0 );
	t( sp_keysize(cur) == 0 );
	t( sp_key(cur) == NULL );
	t( sp_valuesize(cur) == 0 );
	t( sp_value(cur) == NULL );
	t( sp_destroy(cur) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
recover_log_n_get(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPGC, 0) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 0, v;
	while (k < 12) {
		v = k;
		t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
		k++;
	}
	t( sp_destroy(db) == 0 );
	t( exists(dbrep, 1, "log") == 1 );
	db = sp_open(env);
	t( db != NULL );
	k = 0;
	while (k < 12) {
		size_t vsize = 0;
		void *vp = NULL;
		t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 1 );
		t( vsize == sizeof(v) );
		t( *(uint32_t*)vp == k );
		free(vp);
		k++;
	}
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
recover_log_n_replace(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPGC, 0) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( exists(dbrep, 1, "log.incomplete") == 1 );
	t( db != NULL );
	uint32_t k = 0, v = 1;
	while (k < 12) {
		t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
		k++;
	}
	k = 0;
	v = 2;
	while (k < 12) {
		t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
		k++;
	}
	t( sp_destroy(db) == 0 );
	t( exists(dbrep, 1, "log") == 1 );
	db = sp_open(env);
	t( exists(dbrep, 1, "log") == 1 );
	t( exists(dbrep, 2, "log.incomplete") == 1 );
	t( db != NULL );
	k = 0;
	while (k < 12) {
		size_t vsize = 0;
		void *vp = NULL;
		t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 1 );
		t( vsize == sizeof(v) );
		t( *(uint32_t*)vp == 2 );
		free(vp);
		k++;
	}
	t( sp_destroy(db) == 0 );
	t( exists(dbrep, 1, "log") == 1 );
	t( exists(dbrep, 2, "log") == 0 );
	t( exists(dbrep, 2, "log.incomplete") == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
recover_page_set_get(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPGC, 0) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 1;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
	k = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
	k = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
	t( sp_ctl(db, SPMERGEFORCE) == 0 );

	t( exists(dbrep, 1, "log.incomplete") == 0 );
	t( exists(dbrep, 1, "log") == 0 );
	t( exists(dbrep, 1, "db") == 1 );
	t( exists(dbrep, 2, "log.incomplete") == 1 );
	t( sp_destroy(db) == 0 );
	t( exists(dbrep, 1, "db") == 1 );
	t( exists(dbrep, 2, "log") == 0 );
	t( exists(dbrep, 2, "log.incomplete") == 0 );
	db = sp_open(env);
	t( db != NULL );
	size_t vsize = 0;
	void *vp = NULL;
	k = 1;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 1 );
	t( vsize == sizeof(v) );
	t( *(uint32_t*)vp == v );
	free(vp);
	k = 2;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 1 );
	t( vsize == sizeof(v) );
	t( *(uint32_t*)vp == v );
	free(vp);
	k = 3;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 1 );
	t( vsize == sizeof(v) );
	t( *(uint32_t*)vp == v );
	free(vp);
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
recover_page_replace_get(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	t( sp_ctl(env, SPGC, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 1;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
	v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
	v = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	t( exists(dbrep, 1, "log.incomplete") == 0 );
	t( exists(dbrep, 1, "log") == 0 );
	t( exists(dbrep, 2, "log.incomplete") == 1 );
	t( exists(dbrep, 2, "log") == 0 );
	t( exists(dbrep, 1, "db") == 1 );
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	size_t vsize = 0;
	void *vp = NULL;
	k = 1;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 1 );
	t( vsize == sizeof(v) );
	t( *(uint32_t*)vp == 3 );
	free(vp);
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
recover_page_set_get_replace_get(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	t( sp_ctl(env, SPGC, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 1;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
	v = 8;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	size_t vsize = 0;
	void *vp = NULL;
	k = 1;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 1 );
	t( vsize == sizeof(v) );
	t( *(uint32_t*)vp == 8 );
	free(vp);
	v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
	k = 2;
	v = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
	vsize = 0;
	vp = NULL;
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	k = 1;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 1 );
	t( vsize == sizeof(v) );
	t( *(uint32_t*)vp == 2 );
	free(vp);
	k = 2;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 1 );
	t( vsize == sizeof(v) );
	t( *(uint32_t*)vp == 3 );
	free(vp);
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	vsize = 0;
	vp = NULL;
	k = 1;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 1 );
	t( vsize == sizeof(v) );
	t( *(uint32_t*)vp == 2 );
	free(vp);
	k = 2;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 1 );
	t( vsize == sizeof(v) );
	t( *(uint32_t*)vp == 3 );
	free(vp);
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
recover_page_delete_get(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	t( sp_ctl(env, SPGC, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 1;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_delete(db, &k, sizeof(k)) == 0 );
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	t( exists(dbrep, 1, "log.incomplete") == 0 );
	t( exists(dbrep, 1, "log") == 0 );
	t( exists(dbrep, 1, "db.incomplete") == 0 );
	t( exists(dbrep, 1, "db") == 0 );
	t( exists(dbrep, 2, "log.incomplete") == 1 );
	t( exists(dbrep, 2, "log") == 0 );
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	size_t vsize = 0;
	void *vp = NULL;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
recover_page_delete_set_get(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	t( sp_ctl(env, SPGC, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 1;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_delete(db, &k, sizeof(k)) == 0 );
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	t( exists(dbrep, 1, "log.incomplete") == 0 );
	t( exists(dbrep, 1, "log") == 0 );
	t( exists(dbrep, 1, "db.incomplete") == 0 );
	t( exists(dbrep, 1, "db") == 0 );
	t( exists(dbrep, 2, "log.incomplete") == 1 );
	t( exists(dbrep, 2, "log") == 0 );
	t( sp_destroy(db) == 0 );
	t( exists(dbrep, 1, "log.incomplete") == 0 );
	t( exists(dbrep, 1, "log") == 0 );
	t( exists(dbrep, 1, "db.incomplete") == 0 );
	t( exists(dbrep, 1, "db") == 0 );
	t( exists(dbrep, 2, "log.incomplete") == 0 );
	t( exists(dbrep, 2, "log") == 0 );
	db = sp_open(env);
	t( db != NULL );
	size_t vsize = 0;
	void *vp = NULL;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 0 );
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_delete(db, &k, sizeof(k)) == 0 );
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	t( exists(dbrep, 1, "log.incomplete") == 0 );
	t( exists(dbrep, 1, "log") == 0 );
	t( exists(dbrep, 1, "db.incomplete") == 0 );
	t( exists(dbrep, 1, "db") == 1 );
	t( exists(dbrep, 2, "log.incomplete") == 1 );
	t( exists(dbrep, 2, "log") == 0 );
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	vsize = 0;
	vp = NULL;
	k = 1;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 1 );
	t( vsize == sizeof(v) );
	t( *(uint32_t*)vp == 1 );
	free(vp);
	k = 2;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
recover_page_fetch_gte(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPGC, 0) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	void *cur = sp_cursor(db, SPGTE, NULL, 0);
	t( cur != NULL );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 1 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 2 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 3 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 0 );
	t( sp_fetch(cur) == 0 );
	t( sp_destroy(cur) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
recover_page_fetch_lte(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPGC, 0) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	void *cur = sp_cursor(db, SPLTE, NULL, 0);
	t( cur != NULL );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 3 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 2 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 1 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 0 );
	t( sp_fetch(cur) == 0 );
	t( sp_destroy(cur) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
recover_page_fetch_kgte(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPGC, 0) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	k = 2;
	void *cur = sp_cursor(db, SPGTE, &k, sizeof(k));
	t( cur != NULL );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 2 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 3 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 0 );
	t( sp_fetch(cur) == 0 );
	t( sp_destroy(cur) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
recover_page_fetch_kgt(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPGC, 0) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	k = 2;
	void *cur = sp_cursor(db, SPGT, &k, sizeof(k));
	t( cur != NULL );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 3 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 0 );
	t( sp_fetch(cur) == 0 );
	t( sp_destroy(cur) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
recover_page_fetch_klte(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPGC, 0) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	k = 2;
	void *cur = sp_cursor(db, SPLTE, &k, sizeof(k));
	t( cur != NULL );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 2 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 1 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 0 );
	t( sp_fetch(cur) == 0 );
	t( sp_destroy(cur) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
recover_page_fetch_klt(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPGC, 0) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	k = 2;
	void *cur = sp_cursor(db, SPLT, &k, sizeof(k));
	t( cur != NULL );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 1 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 0 );
	t( sp_fetch(cur) == 0 );
	t( sp_destroy(cur) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
recover_page_n_get(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPGC, 0) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 0, v;
	while (k < 12) {
		v = k;
		t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
		k++;
	}
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	t( sp_destroy(db) == 0 );
	t( exists(dbrep, 1, "db") == 1 );
	t( exists(dbrep, 1, "log.incomplete") == 0 );
	t( exists(dbrep, 1, "log") == 0 );
	db = sp_open(env);
	t( db != NULL );
	k = 0;
	while (k < 12) {
		size_t vsize = 0;
		void *vp = NULL;
		t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 1 );
		t( vsize == sizeof(v) );
		t( *(uint32_t*)vp == k );
		free(vp);
		k++;
	}
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
recover_page_n_replace(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPGC, 0) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 0, v = 1;
	while (k < 12) {
		t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
		k++;
	}
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	t( exists(dbrep, 1, "db") == 1 );
	k = 0;
	v = 2;
	while (k < 12) {
		t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
		k++;
	}
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	t( sp_destroy(db) == 0 );
	t( exists(dbrep, 2, "db") == 1 );
	t( exists(dbrep, 1, "log") == 0 );
	t( exists(dbrep, 2, "log") == 0 );
	db = sp_open(env);
	t( db != NULL );
	k = 0;
	while (k < 12) {
		size_t vsize = 0;
		void *vp = NULL;
		t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 1 );
		t( vsize == sizeof(v) );
		t( *(uint32_t*)vp == 2 );
		free(vp);
		k++;
	}
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
recover_page_log_fetch_gte(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPGC, 0) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	k = 4;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 5;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 6;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	void *cur = sp_cursor(db, SPGTE, NULL, 0);
	t( cur != NULL );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 1 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 2 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 3 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 4 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 5 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 6 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 0 );
	t( sp_destroy(cur) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
recover_page_log_fetch_lte(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPGC, 0) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	k = 4;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 5;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 6;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	void *cur = sp_cursor(db, SPLTE, NULL, 0);
	t( cur != NULL );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 6 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 5 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 4 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 3 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 2 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 1 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 0 );
	t( sp_destroy(cur) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
recover_page_log_fetch_kgte(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPGC, 0) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	k = 4;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 5;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 6;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	k = 2;
	void *cur = sp_cursor(db, SPGTE, &k, sizeof(k));
	t( cur != NULL );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 2 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 3 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 4 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 5 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 6 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 0 );
	t( sp_fetch(cur) == 0 );
	t( sp_destroy(cur) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
recover_page_log_fetch_kgt(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPGC, 0) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	k = 4;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 5;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 6;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	k = 2;
	void *cur = sp_cursor(db, SPGT, &k, sizeof(k));
	t( cur != NULL );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 3 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 4 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 5 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 6 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 0 );
	t( sp_fetch(cur) == 0 );
	t( sp_destroy(cur) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
recover_page_log_fetch_klte(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPGC, 0) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	k = 4;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 5;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 6;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	k = 4;
	void *cur = sp_cursor(db, SPLTE, &k, sizeof(k));
	t( cur != NULL );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 4 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 3 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 2 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 1 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 0 );
	t( sp_fetch(cur) == 0 );
	t( sp_destroy(cur) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
recover_page_log_fetch_klt(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPGC, 0) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	k = 4;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 5;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 6;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	k = 4;
	void *cur = sp_cursor(db, SPLT, &k, sizeof(k));
	t( cur != NULL );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 3 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 2 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 1 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 2 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 0 );
	t( sp_fetch(cur) == 0 );
	t( sp_destroy(cur) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
recover_page_log_n_replace(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPGC, 0) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 0, v = 1;
	while (k < 12) {
		t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
		if (k > 0 && k % 3 == 0)
			t( sp_ctl(db, SPMERGEFORCE) == 0 );
		k++;
	}
	k = 0;
	v = 2;
	while (k < 12) {
		t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
		k++;
	}
	t( sp_destroy(db) == 0 );
	t( exists(dbrep, 1, "db") == 1 );
	t( exists(dbrep, 2, "db") == 1 );
	t( exists(dbrep, 3, "db") == 1 );
	t( exists(dbrep, 4, "log") == 1 );
	db = sp_open(env);
	t( db != NULL );
	k = 0;
	while (k < 12) {
		size_t vsize = 0;
		void *vp = NULL;
		t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 1 );
		t( vsize == sizeof(v) );
		t( *(uint32_t*)vp == 2 );
		free(vp);
		k++;
	}
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	t( exists(dbrep, 5, "db") == 1 );
	t( exists(dbrep, 5, "log") == 0 );
	t( exists(dbrep, 5, "log.incomplete") == 0 );
	t( exists(dbrep, 6, "log.incomplete") == 1 );
	k = 0;
	while (k < 12) {
		size_t vsize = 0;
		void *vp = NULL;
		t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 1 );
		t( vsize == sizeof(v) );
		t( *(uint32_t*)vp == 2 );
		free(vp);
		k++;
	}
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

int
main(int argc, char *argv[])
{
	rmrf(dbrep);

	test(recover_log_set_get);
	test(recover_log_replace_get);
	test(recover_log_set_get_replace_get);
	test(recover_log_delete_get);
	test(recover_log_delete_set_get);
	test(recover_log_fetch_gte);
	test(recover_log_fetch_lte);
	test(recover_log_fetch_kgte);
	test(recover_log_fetch_kgt);
	test(recover_log_fetch_klte);
	test(recover_log_fetch_klt);
	test(recover_log_n_get);
	test(recover_log_n_replace);

	test(recover_page_set_get);
	test(recover_page_replace_get);
	test(recover_page_set_get_replace_get);
	test(recover_page_delete_get);
	test(recover_page_delete_set_get);
	test(recover_page_fetch_gte);
	test(recover_page_fetch_lte);
	test(recover_page_fetch_kgte);
	test(recover_page_fetch_kgt);
	test(recover_page_fetch_klte);
	test(recover_page_fetch_klt);
	test(recover_page_n_get);
	test(recover_page_n_replace);

	test(recover_page_log_fetch_gte);
	test(recover_page_log_fetch_lte);
	test(recover_page_log_fetch_kgte);
	test(recover_page_log_fetch_kgt);
	test(recover_page_log_fetch_klte);
	test(recover_page_log_fetch_klt);
	test(recover_page_log_n_replace);
	return 0;
}
