
/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

#include <unistd.h>
#include <fcntl.h>
#include <sophia.h>
#include <sp.h>
#include "test.h"

static char *dbrep = "./rep";
static spa a;

static inline int
cmp(char *a, size_t asz, char *b, size_t bsz, void *arg) {
	register uint32_t av = *(uint32_t*)a;
	register uint32_t bv = *(uint32_t*)b;
	if (av == bv)
		return 0;
	return (av > bv) ? 1 : -1;
}

static void
log_empty(void) {
	spfile f;
	sp_fileinit(&f, &a);
	t( mkdir(dbrep, 0755) == 0 );
	t( sp_lognew(&f, dbrep, 1) == 0 );
	t( sp_logcomplete(&f)  == 0 );
	t( sp_logclose(&f)  == 0 );
	t( exists(dbrep, 1, "log") == 1 );
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db == NULL );
	t( sp_error(env) != NULL );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
log_empty_incomplete(void) {
	spfile f;
	sp_fileinit(&f, &a);
	t( mkdir(dbrep, 0755) == 0 );
	t( sp_lognew(&f, dbrep, 1) == 0 );
	t( sp_logclose(&f)  == 0 );
	t( exists(dbrep, 1, "log.incomplete") == 1 );
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db == NULL );
	t( sp_error(env) != NULL );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
log_badrecord(void) {
	uint32_t k = 123;
	splogh h = {
		.magic = SPMAGIC,
		.version[0] = SP_VERSION_MAJOR,
		.version[1] = SP_VERSION_MINOR
	};
	spvh vh = {
		.crc = 0,
		.size = sizeof(uint32_t),
		.voffset = 0,
		.vsize = sizeof(uint32_t),
		.flags = SPSET
	};
	speofh eof = { SPEOF };
	spfile f;
	sp_fileinit(&f, &a);
	t( mkdir(dbrep, 0755) == 0 );
	t( sp_lognew(&f, dbrep, 1) == 0 );
	sp_logadd(&f, &h, sizeof(h));
	sp_logadd(&f, &vh, sizeof(vh));
	sp_logadd(&f, &k, sizeof(k));
	sp_logadd(&f, &k, sizeof(k));
	sp_logadd(&f, &eof, sizeof(eof));
	t( sp_logflush(&f) == 0 );
	t( sp_logcomplete(&f)  == 0 );
	t( sp_logclose(&f)  == 0 );
	t( exists(dbrep, 1, "log") == 1 );
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db == NULL );
	t( sp_error(env) != NULL );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
log_badrecord_incomplete(void) {
	uint32_t k = 123;
	splogh h = {
		.magic = SPMAGIC,
		.version[0] = SP_VERSION_MAJOR,
		.version[1] = SP_VERSION_MINOR
	};
	spvh vh = {
		.crc = 0,
		.size = sizeof(uint32_t),
		.voffset = 0,
		.vsize = sizeof(uint32_t),
		.flags = SPSET
	};
	speofh eof = { SPEOF };
	spfile f;
	sp_fileinit(&f, &a);
	t( mkdir(dbrep, 0755) == 0 );
	t( sp_lognew(&f, dbrep, 1) == 0 );
	sp_logadd(&f, &h, sizeof(h));
	sp_logadd(&f, &vh, sizeof(vh));
	sp_logadd(&f, &k, sizeof(k));
	sp_logadd(&f, &k, sizeof(k));
	sp_logadd(&f, &eof, sizeof(eof));
	t( sp_logflush(&f) == 0 );
	t( sp_logclose(&f)  == 0 );
	t( exists(dbrep, 1, "log.incomplete") == 1 );
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db == NULL );
	t( sp_error(env) != NULL );
	t( sp_destroy(env) == 0 );
	t( exists(dbrep, 1, "log.incomplete") == 1 );
	t( rmrf(dbrep) == 0 );
}

static void
log_1_badrecord_2_goodrecord(void) {
	uint32_t k = 123;
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	t( sp_set(db, &k, sizeof(k), &k, sizeof(k)) == 0 );
	t( sp_destroy(db) == 0 );
	t( exists(dbrep, 1, "log") == 1 );
	splogh h = {
		.magic = SPMAGIC,
		.version[0] = SP_VERSION_MAJOR,
		.version[1] = SP_VERSION_MINOR
	};
	spvh vh = {
		.crc = 0,
		.size = sizeof(uint32_t),
		.voffset = 0,
		.vsize = sizeof(uint32_t),
		.flags = SPSET
	};
	speofh eof = { SPEOF };
	spfile f;
	sp_fileinit(&f, &a);
	t( sp_lognew(&f, dbrep, 2) == 0 );
	sp_logadd(&f, &h, sizeof(h));
	sp_logadd(&f, &vh, sizeof(vh));
	sp_logadd(&f, &k, sizeof(k));
	sp_logadd(&f, &k, sizeof(k));
	sp_logadd(&f, &eof, sizeof(eof));
	t( sp_logflush(&f) == 0 );
	t( sp_logcomplete(&f)  == 0 );
	t( sp_logclose(&f)  == 0 );
	t( exists(dbrep, 2, "log") == 1 );
	db = sp_open(env);
	t( db == NULL );
	t( sp_destroy(env) == 0 );
	t( exists(dbrep, 1, "log") == 1 );
	t( exists(dbrep, 2, "log") == 1 );
	t( rmrf(dbrep) == 0 );
}

static void
log_noeof(void) {
	uint32_t k = 123;
	splogh h = {
		.magic = SPMAGIC,
		.version[0] = SP_VERSION_MAJOR,
		.version[1] = SP_VERSION_MINOR
	};
	spvh vh = {
		.crc = 0,
		.size = sizeof(uint32_t),
		.voffset = 0,
		.vsize = sizeof(uint32_t),
		.flags = SPSET
	};
	uint32_t crc;
 	crc    = sp_crc32c(0, &k, sizeof(k));
	crc    = sp_crc32c(crc, &k, sizeof(k));
	vh.crc = sp_crc32c(crc, &vh.size, sizeof(spvh) - sizeof(uint32_t));
	spfile f;
	sp_fileinit(&f, &a);
	t( mkdir(dbrep, 0755) == 0 );
	t( sp_lognew(&f, dbrep, 1) == 0 );
	sp_logadd(&f, &h, sizeof(h));
	sp_logadd(&f, &vh, sizeof(vh));
	sp_logadd(&f, &k, sizeof(k));
	sp_logadd(&f, &k, sizeof(k));
	t( sp_logflush(&f) == 0 );
	t( sp_logclose(&f)  == 0 );
	t( exists(dbrep, 1, "log.incomplete") == 1 );
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	size_t vsize = 0;
	void *vp = NULL;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 1 );
	t( vsize == sizeof(k) );
	t( *(uint32_t*)vp == k );
	free(vp);
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
log_noeof_complete(void) {
	uint32_t k = 123;
	splogh h = {
		.magic = SPMAGIC,
		.version[0] = SP_VERSION_MAJOR,
		.version[1] = SP_VERSION_MINOR
	};
	spvh vh = {
		.crc = 0,
		.size = sizeof(uint32_t),
		.voffset = 0,
		.vsize = sizeof(uint32_t),
		.flags = SPSET
	};
	uint32_t crc;
 	crc    = sp_crc32c(0, &k, sizeof(k));
	crc    = sp_crc32c(crc, &k, sizeof(k));
	vh.crc = sp_crc32c(crc, &vh.size, sizeof(spvh) - sizeof(uint32_t));
	spfile f;
	sp_fileinit(&f, &a);
	t( mkdir(dbrep, 0755) == 0 );
	t( sp_lognew(&f, dbrep, 1) == 0 );
	sp_logadd(&f, &h, sizeof(h));
	sp_logadd(&f, &vh, sizeof(vh));
	sp_logadd(&f, &k, sizeof(k));
	sp_logadd(&f, &k, sizeof(k));
	t( sp_logflush(&f) == 0 );
	t( sp_logcomplete(&f)  == 0 );
	t( sp_logclose(&f)  == 0 );
	t( exists(dbrep, 1, "log") == 1 );
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db == NULL );
	t( sp_error(env) != NULL );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
db_empty(void) {
	spfile f;
	sp_fileinit(&f, &a);
	t( mkdir(dbrep, 0755) == 0 );
	char path[1024];
	snprintf(path, sizeof(path), "%s/%"PRIu32".db",
	         dbrep, 1);
	int fd = open(path, O_CREAT|O_RDWR, 0644);
	t( fd != -1 );
	t( close(fd) == 0 );
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db == NULL );
	t( sp_error(env) != NULL );
	t( exists(dbrep, 1, "db") == 1 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
db_empty_incomplete(void) {
	spfile f;
	sp_fileinit(&f, &a);
	t( mkdir(dbrep, 0755) == 0 );
	char path[1024];
	snprintf(path, sizeof(path), "%s/%"PRIu32".db.incomplete",
	         dbrep, 1);
	int fd = open(path, O_CREAT|O_RDWR, 0644);
	t( fd != -1 );
	t( close(fd) == 0 );
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db == NULL );
	t( sp_error(env) != NULL );
	t( exists(dbrep, 1, "db.incomplete") == 1 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
db_badpage(void) {
	sppageh h = {
		.crc = 0,
		.count = 0,
		.size = 1234,
		.bsize = 1234,
	};
	t( mkdir(dbrep, 0755) == 0 );
	char path[1024];
	snprintf(path, sizeof(path), "%s/%"PRIu32".db",
	         dbrep, 1);
	int fd = open(path, O_CREAT|O_RDWR, 0644);
	t( fd != -1 );
	t( write(fd, &h, sizeof(h)) == sizeof(h) );
	t( close(fd) == 0 );
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db == NULL );
	t( sp_error(env) != NULL );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
log_incomplete_db_incomplete(void) {
	uint32_t k = 123;
	splogh h = {
		.magic = SPMAGIC,
		.version[0] = SP_VERSION_MAJOR,
		.version[1] = SP_VERSION_MINOR
	};
	spvh vh = {
		.crc = 0,
		.size = sizeof(uint32_t),
		.voffset = 0,
		.vsize = sizeof(uint32_t),
		.flags = SPSET
	};
	speofh eof = { SPEOF };
	uint32_t crc;
 	crc    = sp_crc32c(0, &k, sizeof(k));
	crc    = sp_crc32c(crc, &k, sizeof(k));
	vh.crc = sp_crc32c(crc, &vh.size, sizeof(spvh) - sizeof(uint32_t));
	spfile f;
	sp_fileinit(&f, &a);
	t( mkdir(dbrep, 0755) == 0 );
	t( sp_lognew(&f, dbrep, 1) == 0 );
	sp_logadd(&f, &h, sizeof(h));
	sp_logadd(&f, &vh, sizeof(vh));
	sp_logadd(&f, &k, sizeof(k));
	sp_logadd(&f, &k, sizeof(k));
	sp_logadd(&f, &eof, sizeof(eof));
	t( sp_logflush(&f) == 0 );
	t( sp_logclose(&f)  == 0 );
	char path[1024];
	snprintf(path, sizeof(path), "%s/%"PRIu32".db.incomplete",
	         dbrep, 1);
	int fd = open(path, O_CREAT|O_RDWR, 0644);
	t( fd != -1 );
	t( write(fd, &h, sizeof(h)) == sizeof(h) );
	t( close(fd) == 0 );
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db == NULL );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

static void
log_db_incomplete(void) {
	uint32_t k = 123;
	splogh h = {
		.magic = SPMAGIC,
		.version[0] = SP_VERSION_MAJOR,
		.version[1] = SP_VERSION_MINOR
	};
	spvh vh = {
		.crc = 0,
		.size = sizeof(uint32_t),
		.voffset = 0,
		.vsize = sizeof(uint32_t),
		.flags = SPSET
	};
	speofh eof = { SPEOF };
	uint32_t crc;
 	crc    = sp_crc32c(0, &k, sizeof(k));
	crc    = sp_crc32c(crc, &k, sizeof(k));
	vh.crc = sp_crc32c(crc, &vh.size, sizeof(spvh) - sizeof(uint32_t));
	spfile f;
	sp_fileinit(&f, &a);
	t( mkdir(dbrep, 0755) == 0 );
	t( sp_lognew(&f, dbrep, 1) == 0 );
	sp_logadd(&f, &h, sizeof(h));
	sp_logadd(&f, &vh, sizeof(vh));
	sp_logadd(&f, &k, sizeof(k));
	sp_logadd(&f, &k, sizeof(k));
	sp_logadd(&f, &eof, sizeof(eof));
	t( sp_logflush(&f) == 0 );
	t( sp_logcomplete(&f) == 0 );
	t( sp_logclose(&f)  == 0 );
	t( exists(dbrep, 1, "log") == 1 );
	char path[1024];
	snprintf(path, sizeof(path), "%s/%"PRIu32".db.incomplete",
	         dbrep, 1);
	int fd = open(path, O_CREAT|O_RDWR, 0644);
	t( fd != -1 );
	t( write(fd, &h, sizeof(h)) == sizeof(h) );
	t( close(fd) == 0 );
	void *env = sp_env();
	t( env != NULL );
	t( sp_ctl(env, SPDIR, SPO_CREAT|SPO_RDWR, dbrep) == 0 );
	t( sp_ctl(env, SPCMP, cmp, NULL) == 0 );
	t( sp_ctl(env, SPMERGE, 0) == 0 );
	void *db = sp_open(env);
	t( db != NULL );
	size_t vsize = 0;
	void *vp = NULL;
	t( sp_get(db, &k, sizeof(k), &vp, &vsize) == 1 );
	t( vsize == sizeof(k) );
	t( *(uint32_t*)vp == k );
	free(vp);
	t( sp_destroy(db) == 0 );
	t( sp_destroy(env) == 0 );
	t( rmrf(dbrep) == 0 );
}

int
main(int argc, char *argv[])
{
	sp_allocinit(&a, sp_allocstd, NULL);
	rmrf(dbrep);

	test(log_empty);
	test(log_empty_incomplete);
	test(log_badrecord);
	test(log_badrecord_incomplete);
	test(log_1_badrecord_2_goodrecord);
	test(log_noeof);
	test(log_noeof_complete);

	test(db_empty);
	test(db_empty_incomplete);
	test(db_badpage);

	test(log_db_incomplete);
	test(log_incomplete_db_incomplete);
	return 0;
}
