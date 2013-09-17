#ifndef SP_FILE_H_
#define SP_FILE_H_

/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

typedef struct spfile spfile;

struct spfile {
	spa *a;
	int creat;
	uint64_t used;
	uint64_t size;
	uint64_t svp;
	char *file;
	int fd;
	char *map;
	struct iovec iov[8];
	int iovc;
};

int sp_fileexists(char*);
int sp_filerm(char*);

static inline void
sp_fileinit(spfile *f, spa *a) {
	memset(f, 0, sizeof(*f));
	f->a = a;
	f->fd = -1;
}

static inline void
sp_filesvp(spfile *f) {
	f->svp = f->used;
}

int sp_mapopen(spfile*, char*);
int sp_mapnew(spfile*, char*, uint64_t);
int sp_mapclose(spfile*);
int sp_mapcomplete(spfile*);
int sp_mapunmap(spfile*);
int sp_mapunlink(spfile*);
int sp_mapensure(spfile*, uint64_t, float);

static inline int
sp_mapepoch(spfile *f, char *dir, uint32_t epoch, char *ext) {
	char path[1024];
	snprintf(path, sizeof(path), "%s/%"PRIu32".%s", dir, epoch, ext);
	return sp_mapopen(f, path);
}

static inline int
sp_mapepochnew(spfile *f, uint64_t size,
               char *dir, uint32_t epoch, char *ext) {
	char path[1024];
	snprintf(path, sizeof(path), "%s/%"PRIu32".%s.incomplete", dir, epoch, ext);
	return sp_mapnew(f, path, size);
}

static inline void
sp_mapuse(spfile *f, size_t size) {
	f->used += size;
	assert(f->used <= f->size);
}

static inline void
sp_maprlb(spfile *f) {
	f->used = f->svp;
}

static inline int
sp_mapinbound(spfile *f, size_t off) {
	return off <= f->size;
}

int sp_lognew(spfile*, char*, uint32_t);
int sp_logcontinue(spfile*, char*, uint32_t);
int sp_logclose(spfile*);
int sp_logcomplete(spfile*);
int sp_logcompleteforce(spfile*);
int sp_logunlink(spfile*);
int sp_logflush(spfile*);
int sp_logrlb(spfile*);
int sp_logeof(spfile*);

static inline void
sp_logadd(spfile *f, const void *buf, int size) {
	assert(f->iovc < 8);
	f->iov[f->iovc].iov_base = (void*)buf;
	f->iov[f->iovc].iov_len = size;
	f->iovc++;
}

static inline int
sp_epochrm(char *dir, uint32_t epoch, char *ext) {
	char path[1024];
	snprintf(path, sizeof(path), "%s/%"PRIu32".%s", dir, epoch, ext);
	return sp_filerm(path);
}

#endif
