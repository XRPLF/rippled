
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
merge_liveonly(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( exists(dbrep, 1, "log.incomplete") == 1 );
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	t( exists(dbrep, 1, "log.incomplete") == 0 );
	t( exists(dbrep, 1, "log") == 0 );
	t( exists(dbrep, 1, "db") == 0 );
	t( exists(dbrep, 1, "db.incomplete") == 0 );
	t( exists(dbrep, 2, "log.incomplete") == 1 );
	t( exists(dbrep, 2, "log") == 0 );
	t( sp_destroy(db) == 0 );
	t( exists(dbrep, 1, "log") == 0 );
	t( exists(dbrep, 2, "log") == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
merge_phase0(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( exists(dbrep, 1, "log.incomplete") == 1 );
	uint32_t k = 1, v = 1;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	t( exists(dbrep, 1, "log.incomplete") == 0 );
	t( exists(dbrep, 1, "log") == 0 );
	t( exists(dbrep, 1, "db.incomplete") == 0 );
	t( exists(dbrep, 1, "db") == 1 );
	t( exists(dbrep, 2, "log.incomplete") == 1 );
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	t( sp_destroy(db) == 0 );
	t( exists(dbrep, 1, "log.incomplete") == 0 );
	t( exists(dbrep, 1, "log") == 0 );
	t( exists(dbrep, 1, "db.incomplete") == 0 );
	t( exists(dbrep, 1, "db") == 1 );
	t( exists(dbrep, 2, "log.incomplete") == 0 );
	t( exists(dbrep, 2, "log") == 0 );
	t( exists(dbrep, 2, "db") == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
merge_phase1(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPGC, 0) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( exists(dbrep, 1, "log.incomplete") == 1 );
	uint32_t k = 1, v = 1;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	t( exists(dbrep, 1, "log.incomplete") == 0 );
	t( exists(dbrep, 1, "log") == 0 );
	t( exists(dbrep, 1, "db.incomplete") == 0 );
	t( exists(dbrep, 1, "db") == 1 );
	t( exists(dbrep, 2, "log.incomplete") == 1 );
	t( exists(dbrep, 2, "log") == 0 );
	k = 4;	
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 5;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 6;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	t( exists(dbrep, 1, "db") == 1 );
	t( exists(dbrep, 2, "db.incomplete") == 0 );
	t( exists(dbrep, 2, "db") == 1 );
	t( exists(dbrep, 2, "log.incomplete") == 0 );
	t( exists(dbrep, 2, "log") == 0 );
	t( exists(dbrep, 3, "log.incomplete") == 1 );
	t( exists(dbrep, 3, "log") == 0 );
	t( sp_destroy(db) == 0 );
	t( exists(dbrep, 1, "db") == 1 );
	t( exists(dbrep, 2, "db.incomplete") == 0 );
	t( exists(dbrep, 2, "db") == 1 );
	t( exists(dbrep, 2, "log.incomplete") == 0 );
	t( exists(dbrep, 2, "log") == 0 );
	t( exists(dbrep, 3, "log.incomplete") == 0 );
	t( exists(dbrep, 3, "log") == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
merge_phase1gc(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPGC, 1) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( exists(dbrep, 1, "log.incomplete") == 1 );
	uint32_t k = 1, v = 1;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 2;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 3;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	t( exists(dbrep, 1, "log.incomplete") == 0 );
	t( exists(dbrep, 1, "log") == 0 );
	t( exists(dbrep, 1, "db.incomplete") == 0 );
	t( exists(dbrep, 1, "db") == 1 );
	t( exists(dbrep, 2, "log.incomplete") == 1 );
	t( exists(dbrep, 2, "log") == 0 );
	k = 4;	
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 5;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	k = 6;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	t( exists(dbrep, 1, "db") == 0 );
	t( exists(dbrep, 2, "db.incomplete") == 0 );
	t( exists(dbrep, 2, "db") == 1 );
	t( exists(dbrep, 2, "log.incomplete") == 0 );
	t( exists(dbrep, 2, "log") == 0 );
	t( exists(dbrep, 3, "log.incomplete") == 1 );
	t( exists(dbrep, 3, "log") == 0 );
	t( sp_destroy(db) == 0 );
	t( exists(dbrep, 1, "db") == 0 );
	t( exists(dbrep, 2, "db.incomplete") == 0 );
	t( exists(dbrep, 2, "db") == 1 );
	t( exists(dbrep, 2, "log.incomplete") == 0 );
	t( exists(dbrep, 2, "log") == 0 );
	t( exists(dbrep, 3, "log.incomplete") == 0 );
	t( exists(dbrep, 3, "log") == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
merge_phase1n(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPGC, 0) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	uint32_t k = 1, v = 1;
	/* 1 */
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	/* 2 */
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	/* 3 */
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	/* 4 */
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	/* 5 */
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	/* merge */
	t( exists(dbrep, 1, "db") == 1 );
	t( exists(dbrep, 2, "db") == 1 );
	t( exists(dbrep, 3, "db") == 1 );
	t( exists(dbrep, 4, "db") == 1 );
	t( exists(dbrep, 5, "db") == 1 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
merge_phase1ngc(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPGC, 1) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	uint32_t k = 1, v = 1;
	/* 1 */
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	/* 2 */
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	/* 3 */
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	/* 4 */
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	/* 5 */
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0 );
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	/* merge */
	t( exists(dbrep, 1, "db") == 0 );
	t( exists(dbrep, 2, "db") == 0 );
	t( exists(dbrep, 3, "db") == 0 );
	t( exists(dbrep, 4, "db") == 0 );
	t( exists(dbrep, 5, "db") == 1 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
merge_delete(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPGC, 0) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 0, v = 1;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
	k = 1;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
	t( sp_destroy(db) == 0 );
	t( exists(dbrep, 1, "log") == 1 );
	t( exists(dbrep, 1, "log.incomplete") == 0 );
	db = sp_open(env);
	t( db != NULL );
	k = 0;
	t( sp_delete(db, &k, sizeof(k)) == 0 );
	k = 1;
	t( sp_delete(db, &k, sizeof(k)) == 0 );
	t( exists(dbrep, 1, "log") == 1 );
	t( exists(dbrep, 2, "log.incomplete") == 1 );
	t( exists(dbrep, 2, "log") == 0 );
	t( sp_ctl(db, SPMERGEFORCE) == 0);
	t( exists(dbrep, 1, "log") == 0 );
	t( exists(dbrep, 2, "log.incomplete") == 0 );
	t( exists(dbrep, 2, "log") == 0 );
	t( exists(dbrep, 3, "log.incomplete") == 1 );
	t( exists(dbrep, 1, "db") == 0 );
	t( exists(dbrep, 2, "db") == 0 );
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	k = 0;
	size_t vsize = 0;
	void *vp = NULL;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 0 );
	k = 1;
	vsize = 0;
	vp = NULL;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
merge_deletegc(void) {
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPGC, 1) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	uint32_t k = 0, v = 1;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
	k = 1;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
	t( sp_destroy(db) == 0 );
	t( exists(dbrep, 1, "log") == 1 );
	t( exists(dbrep, 1, "log.incomplete") == 0 );
	db = sp_open(env);
	t( db != NULL );
	k = 0;
	t( sp_delete(db, &k, sizeof(k)) == 0 );
	k = 1;
	t( sp_delete(db, &k, sizeof(k)) == 0 );
	t( exists(dbrep, 1, "log") == 1 );
	t( exists(dbrep, 2, "log.incomplete") == 1 );
	t( exists(dbrep, 2, "log") == 0 );
	t( sp_ctl(db, SPMERGEFORCE) == 0 );
	t( exists(dbrep, 1, "log") == 0 );
	t( exists(dbrep, 2, "log.incomplete") == 0 );
	t( exists(dbrep, 2, "log") == 0 );
	t( exists(dbrep, 1, "db") == 0 );
	t( exists(dbrep, 2, "db") == 0 );
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	k = 0;
	size_t vsize = 0;
	void *vp = NULL;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 0 );
	k = 1;
	vsize = 0;
	vp = NULL;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
merge_delete_log_n(void) {
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
	t( sp_destroy(db) == 0 );
	t( exists(dbrep, 1, "log") == 1 );
	db = sp_open(env);
	t( db != NULL );
	k = 0;
	while (k < 12) {
		t( sp_delete(db, &k, sizeof(k)) == 0);
		k++;
	}
	t( exists(dbrep, 1, "log.incomplete") == 0 );
	t( exists(dbrep, 1, "log") == 1 );
	t( exists(dbrep, 2, "log.incomplete") == 1 );
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	k = 0;
	while (k < 12) {
		size_t vsize = 0;
		void *vp = NULL;
		t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 0 );
		k++;
	}
	t( sp_destroy(db) == 0 );
	t( exists(dbrep, 1, "log") == 1 );
	t( exists(dbrep, 2, "log") == 1 );
	t( exists(dbrep, 3, "log") == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
merge_delete_page_n(void) {
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
	t( sp_destroy(db) == 0 );
	t( exists(dbrep, 1, "log") == 1 );
	db = sp_open(env);
	t( db != NULL );
	k = 0;
	while (k < 12) {
		t( sp_delete(db, &k, sizeof(k)) == 0);
		k++;
	}
	t( exists(dbrep, 1, "log") == 1 );
	t( exists(dbrep, 2, "log.incomplete") == 1 );
	t( sp_ctl(db, SPMERGEFORCE) == 0);
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	k = 0;
	while (k < 12) {
		size_t vsize = 0;
		void *vp = NULL;
		t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 0 );
		k++;
	}
	t( sp_destroy(db) == 0 );
	t( exists(dbrep, 1, "log") == 0 );
	t( exists(dbrep, 2, "log") == 0 );
	t( exists(dbrep, 2, "db") == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
merge_delete_page_log_n(void) {
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
	t( sp_ctl(db, SPMERGEFORCE) == 0);
	t( exists(dbrep, 1, "db") == 1 );
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	k = 0;
	while (k < 12) {
		t( sp_delete(db, &k, sizeof(k)) == 0);
		k++;
	}
	t( sp_ctl(db, SPMERGEFORCE) == 0);
	t( exists(dbrep, 2, "log.incomplete") == 0 );
	t( exists(dbrep, 2, "db") == 1 );
	t( exists(dbrep, 3, "log.incomplete") == 1 );
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	k = 0;
	while (k < 12) {
		size_t vsize = 0;
		void *vp = NULL;
		t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 0 );
		k++;
	}
	t( sp_destroy(db) == 0 );
	t( exists(dbrep, 1, "db") == 1 );
	t( exists(dbrep, 2, "db") == 1 );
	t( exists(dbrep, 3, "log") == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
merge_delete_page_log_n_even(void) {
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
		v = k;
		t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
		k++;
	}
	t( sp_ctl(db, SPMERGEFORCE) == 0);
	t( sp_destroy(db) == 0 );
	t( exists(dbrep, 1, "db") == 1 );
	db = sp_open(env);
	t( db != NULL );
	k = 0;
	while (k < 12) {
		if (k > 0 && (k % 2) == 0)
			t( sp_delete(db, &k, sizeof(k)) == 0);
		k++;
	}
	t( exists(dbrep, 2, "log.incomplete") == 1 );
	t( sp_ctl(db, SPMERGEFORCE) == 0);
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	k = 0;
	while (k < 12) {
		size_t vsize = 0;
		void *vp = NULL;
		if (k == 0 || (k % 2) != 0) {
			t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 1 );
			t( vsize == sizeof(v) );
			t( *(uint32_t*)vp == k );
			free(vp);
		}
		k++;
	}
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
merge_delete_page_log_n_extra(void) {
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
	t( sp_destroy(db) == 0 );
	t( exists(dbrep, 1, "log") == 1 );
	db = sp_open(env);
	t( db != NULL );
	k = 0;
	while (k < 12) {
		t( sp_delete(db, &k, sizeof(k)) == 0);
		k++;
	}
	k = 13;
	v = k;
	t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
	t( exists(dbrep, 2, "log.incomplete") == 1 );
	t( sp_ctl(db, SPMERGEFORCE) == 0);
	t( exists(dbrep, 1, "db") == 0 );
	t( exists(dbrep, 2, "db") == 1 );
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	k = 0;
	while (k < 12) {
		size_t vsize = 0;
		void *vp = NULL;
		t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 0 );
		k++;
	}
	k = 13;
	size_t vsize = 0;
	void *vp = NULL;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 1 );
	t( vsize == sizeof(v) );
	t( *(uint32_t*)vp == 13 );
	free(vp);
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
merge_delete_page_log_n_fetch_gte(void) {
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
		v = k;
		t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
		k++;
	}
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	k = 0;
	while (k < 12) {
		if (k > 0 && (k % 2) == 0)
			t( sp_delete(db, &k, sizeof(k)) == 0);
		k++;
	}
	t( sp_ctl(db, SPMERGEFORCE) == 0);
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	void *cur = sp_cursor(db, SPGTE, NULL, 0);
	t( cur != NULL );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 0 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 0 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 1 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 1 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 3 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 3 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 5 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 5 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 7 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 7 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 9 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 9 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 11 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 11 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 0 );
	t( sp_destroy(cur) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
merge_delete_page_log_n_fetch_lte(void) {
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
		v = k;
		t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
		k++;
	}
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	k = 0;
	while (k < 12) {
		if (k > 0 && (k % 2) == 0)
			t( sp_delete(db, &k, sizeof(k)) == 0);
		k++;
	}
	t( sp_ctl(db, SPMERGEFORCE) == 0);
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	void *cur = sp_cursor(db, SPLTE, NULL, 0);
	t( cur != NULL );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 11 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 11 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 9 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 9 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 7 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 7 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 5 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 5 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 3 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 3 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 1 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 1 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 0 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 0 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 0 );
	t( sp_destroy(cur) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
merge_delete_page_log_n_fetch_kgte(void) {
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
		v = k;
		t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
		k++;
	}
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	k = 0;
	while (k < 12) {
		if (k > 0 && (k % 2) == 0)
			t( sp_delete(db, &k, sizeof(k)) == 0);
		k++;
	}
	t( sp_ctl(db, SPMERGEFORCE) == 0);
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	k = 6;
	void *cur = sp_cursor(db, SPGTE, &k, sizeof(k));
	t( cur != NULL );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 7 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 7 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 9 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 9 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 11 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 11 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 0 );
	t( sp_destroy(cur) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
merge_delete_page_log_n_fetch_klte(void) {
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
		v = k;
		t( sp_set(db, &k, sizeof(k), &v, sizeof(v)) == 0);
		k++;
	}
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	k = 0;
	while (k < 12) {
		if (k > 0 && (k % 2) == 0)
			t( sp_delete(db, &k, sizeof(k)) == 0);
		k++;
	}
	t( sp_ctl(db, SPMERGEFORCE) == 0);
	t( sp_destroy(db) == 0 );
	db = sp_open(env);
	t( db != NULL );
	k = 6;
	void *cur = sp_cursor(db, SPLTE, &k, sizeof(k));
	t( cur != NULL );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 5 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 5 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 3 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 3 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 1 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 1 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 1 );
	t( *(uint32_t*)sp_key(cur) == 0 );
	t( sp_keysize(cur) == sizeof(k) );
	t( *(uint32_t*)sp_value(cur) == 0 );
	t( sp_valuesize(cur) == sizeof(v) );
	t( sp_fetch(cur) == 0 );
	t( sp_destroy(cur) == 0 );
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

int
main(int argc, char *argv[])
{
	rmrf(dbrep);

	test(merge_liveonly);
	test(merge_phase0);
	test(merge_phase1);
	test(merge_phase1gc);
	test(merge_phase1n);
	test(merge_phase1ngc);

	test(merge_delete);
	test(merge_deletegc);
	test(merge_delete_log_n);
	test(merge_delete_page_n);
	test(merge_delete_page_log_n);
	test(merge_delete_page_log_n_even);
	test(merge_delete_page_log_n_extra);
	test(merge_delete_page_log_n_fetch_gte);
	test(merge_delete_page_log_n_fetch_lte);
	test(merge_delete_page_log_n_fetch_kgte);
	test(merge_delete_page_log_n_fetch_klte);
	return 0;
}
