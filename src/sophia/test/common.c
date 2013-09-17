
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
env(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_destroy(env) == 0 );
}

static void
env_opts(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPALLOC, NULL, NULL) == 0 );
	t( sp_ctl(env, SPPAGE, 1024) == 0 );
	t( sp_ctl(env, SPGC, 0.5) == 0 );
	t( sp_ctl(env, SPGROW, 16 * 1024 * 1024, 2.0) == 0 );
	t( sp_ctl(env, SPMERGE, 1) == 0 );
	uint32_t major, minor;
	t( sp_ctl(NULL, SPVERSION, &major, &minor) == 0 );
	t( major == 1 );
	t( minor == 1 );
	t( sp_destroy(env) == 0 );
}

static void
open_ro_creat(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDONLY, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db == NULL );
	t( sp_error(env) != NULL );
	t( sp_destroy(env) == 0 );
}

static void
open_rdwr(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db == NULL );
	t( sp_error(env) != NULL );
	t( sp_destroy(env) == 0 );
}

static void
open_rdwr_creat(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
open_reopen(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
open_reopen_ro(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	t( sp_destroy(db) == 0 );
	t( sp_ctl(env, SPDIR, SPO_RDONLY, dbrep) == 0 );
	db = sp_open(env);
	t( db != NULL );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
set(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 1;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
set_get(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 1;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	size_t vsize = 0;
	void *vp = NULL;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 1 );
	t( vsize == sizeof(v) );
	t( *(uint32_t*)vp == v );
	free(vp);
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
set_get_zerovalue(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1;
	t( sp_set(db, &k, sizeof(k), NULL, 0) == 0 );
	size_t vsize = 0;
	void *vp = NULL;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 1 );
	t( vsize == 0 );
	t( vp == NULL );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
replace(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 1;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
replace_get(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 1;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
	size_t vsize = 0;
	void *vp = NULL;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 1 );
	t( vsize == sizeof(v) );
	t( *(uint32_t*)vp == 1 );
	free(vp);
	v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 1 );
	t( vsize == sizeof(v) );
	t( *(uint32_t*)vp == 2 );
	free(vp);
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
set_delete(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 1;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_delete(db, &k, sizeof(k)) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
set_delete_get(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 1;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_delete(db, &k, sizeof(k)) == 0 );
	size_t vsize = 0;
	void *vp = NULL;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
set_delete_set_get(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 1;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_delete(db, &k, sizeof(k)) == 0 );
	v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	size_t vsize = 0;
	void *vp = NULL;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 1 );
	t( vsize == sizeof(v) );
	t( *(uint32_t*)vp == 2 );
	free(vp);
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
delete(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1;
	t( sp_delete(db, &k, sizeof(k)) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
delete_set_get(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 1;
	t( sp_delete(db, &k, sizeof(k)) == 0 );
	v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	size_t vsize = 0;
	void *vp = NULL;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 1 );
	t( vsize == sizeof(v) );
	t( *(uint32_t*)vp == 2 );
	free(vp);
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
cursor(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	void *cur = sp_cursor(db, SPGTE, NULL, 0);
	t( cur != NULL );
	t( sp_destroy(cur) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
fetch_gte_empty(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	void *cur = sp_cursor(db, SPGTE, NULL, 0);
	t( cur != NULL );
	t( sp_fetch(cur) == 0 );
	t( sp_fetch(cur) == 0 );
	t( sp_destroy(cur) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
fetch_gt_empty(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	void *cur = sp_cursor(db, SPGT, NULL, 0);
	t( cur != NULL );
	t( sp_fetch(cur) == 0 );
	t( sp_fetch(cur) == 0 );
	t( sp_destroy(cur) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
fetch_lte_empty(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	void *cur = sp_cursor(db, SPLTE, NULL, 0);
	t( cur != NULL );
	t( sp_fetch(cur) == 0 );
	t( sp_fetch(cur) == 0 );
	t( sp_destroy(cur) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
fetch_lt_empty(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	void *cur = sp_cursor(db, SPLT, NULL, 0);
	t( cur != NULL );
	t( sp_fetch(cur) == 0 );
	t( sp_fetch(cur) == 0 );
	t( sp_destroy(cur) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
fetch_kgte_empty(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1;
	void *cur = sp_cursor(db, SPGTE, &k, sizeof(k));
	t( cur != NULL );
	t( sp_fetch(cur) == 0 );
	t( sp_fetch(cur) == 0 );
	t( sp_destroy(cur) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
fetch_kgt_empty(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1;
	void *cur = sp_cursor(db, SPGT, &k, sizeof(k));
	t( cur != NULL );
	t( sp_fetch(cur) == 0 );
	t( sp_fetch(cur) == 0 );
	t( sp_destroy(cur) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
fetch_klte_empty(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1;
	void *cur = sp_cursor(db, SPLTE, &k, sizeof(k));
	t( cur != NULL );
	t( sp_fetch(cur) == 0 );
	t( sp_fetch(cur) == 0 );
	t( sp_destroy(cur) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
fetch_klt_empty(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1;
	void *cur = sp_cursor(db, SPLT, &k, sizeof(k));
	t( cur != NULL );
	t( sp_fetch(cur) == 0 );
	t( sp_fetch(cur) == 0 );
	t( sp_destroy(cur) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
fetch_gte(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
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
fetch_gt(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	void *cur = sp_cursor(db, SPGT, NULL, 0);
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
fetch_lte(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
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
fetch_lt(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	void *cur = sp_cursor(db, SPLT, NULL, 0);
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
fetch_kgte(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );

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
fetch_kgt(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
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
fetch_klte(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 2;
	void *cur = sp_cursor(db, SPLTE, &k, sizeof(k) );
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
fetch_klt(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 2;
	void *cur = sp_cursor(db, SPLT, &k, sizeof(k) );
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
fetch_after_end(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
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
	t( sp_keysize(cur) == 0 );
	t( sp_key(cur) == NULL );
	t( sp_valuesize(cur) == 0 );
	t( sp_value(cur) == NULL );
	t( sp_destroy(cur) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

#if 0
static void
rotate_empty(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	t( sp_ctl(env, SPTTL, 2) == 0 );
	void *db = sp_open(env);
	t( exists(dbrep, 1, "log.incomplete") == 1 );
	t( sp_destroy(db) == 0 );
	t( exists(dbrep, 1, "log.incomplete") == 0 );
	t( exists(dbrep, 1, "log") == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
rotate(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	t( sp_ctl(env, SPTTL, 2) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 1, v = 1;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
	t( exists(dbrep, 1, "log.incomplete") == 1 );
	t( exists(dbrep, 1, "db.incomplete") == 0 );
	t( exists(dbrep, 1, "db") == 0 );
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
	t( exists(dbrep, 1, "log.incomplete") == 1 );
	t( exists(dbrep, 2, "log.incomplete") == 1 );
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
	t( exists(dbrep, 3, "log.incomplete") == 0 );
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
	t( exists(dbrep, 1, "log.incomplete") == 1 );
	t( exists(dbrep, 2, "log.incomplete") == 1 );
	t( exists(dbrep, 3, "log.incomplete") == 1 );
	t( sp_destroy(db) == 0 );
	t( exists(dbrep, 1, "db.incomplete") == 0 );
	t( exists(dbrep, 1, "db") == 0 );
	t( exists(dbrep, 1, "log.incomplete") == 0 );
	t( exists(dbrep, 2, "log.incomplete") == 0 );
	t( exists(dbrep, 3, "log.incomplete") == 0 );
	t( exists(dbrep, 1, "log") == 1 );
	t( exists(dbrep, 2, "log") == 1 );
	t( exists(dbrep, 3, "log") == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}
#endif

int
main(int argc, char *argv[])
{
	rmrf(dbrep);

	test(env);
	test(env_opts);
	test(open_ro_creat);
	test(open_rdwr);
	test(open_rdwr_creat);
	test(open_reopen);
	test(open_reopen_ro);
	test(set);
	test(set_get);
	test(set_get_zerovalue);
	test(replace);
	test(replace_get);
	test(set_delete);
	test(set_delete_get);
	test(set_delete_set_get);
	test(delete);
	test(delete_set_get);
	test(cursor);
	test(fetch_gte_empty);
	test(fetch_gt_empty);
	test(fetch_lte_empty);
	test(fetch_lt_empty);
	test(fetch_kgte_empty);
	test(fetch_kgt_empty);
	test(fetch_klte_empty);
	test(fetch_klt_empty);
	test(fetch_gte);
	test(fetch_gt);
	test(fetch_lte);
	test(fetch_lt);
	test(fetch_kgte);
	test(fetch_kgt);
	test(fetch_klte);
	test(fetch_klt);
	test(fetch_after_end);
	/*
	test(rotate_empty);
	test(rotate);
	*/
	return 0;
}
