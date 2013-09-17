#ifndef SP_KEY_H_
#define SP_KEY_H_

/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

typedef struct spref spref;
typedef struct sprefset sprefset;

#define SPREFNONE 0
#define SPREFD    1
#define SPREFM    2

struct spref {
	uint8_t type;
	union {
		spvh *vh;
		spv *v;
	} v;
} sppacked;

struct sprefset {
	spref *r;
	int used;
	int max;
};

static inline char*
sp_refk(spref *r) {
	switch (r->type) {
	case SPREFD: return r->v.vh->key;
	case SPREFM: return r->v.v->key;
	}
	return NULL;
}

static inline size_t
sp_refksize(spref *r) {
	switch (r->type) {
	case SPREFD: return r->v.vh->size;
	case SPREFM: return r->v.v->size;
	}
	return 0;
}

static inline char*
sp_refv(spref *r, char *p) {
	switch (r->type) {
	case SPREFD: return p + r->v.vh->voffset;
	case SPREFM: return sp_vv(r->v.v);
	}
	return NULL;
}

static inline size_t
sp_refvsize(spref *r) {
	switch (r->type) {
	case SPREFD: return r->v.vh->vsize;
	case SPREFM: return sp_vvsize(r->v.v);
	}
	return 0;
}

static inline int
sp_refisdel(spref *r) {
	register int flags = 0;
	switch (r->type) {
	case SPREFM: flags = r->v.v->flags;
		break;
	case SPREFD: flags = r->v.vh->flags;
		break;
	}
	return (flags & SPDEL? 1: 0);
}

static inline int
sp_refsetinit(sprefset *s, spa *a, int count) {
	s->r = sp_malloc(a, count * sizeof(spref));
	if (spunlikely(s->r == NULL))
		return -1;
	s->used = 0;
	s->max = count;
	return 0;
}

static inline void
sp_refsetfree(sprefset *s, spa *a) {
	if (s->r) {
		sp_free(a, s->r);
		s->r = NULL;
	}
}

static inline void
sp_refsetadd(sprefset *s, spref *r) {
	assert(s->used < s->max);
	s->r[s->used] = *r;
	s->used++;
}

static inline void
sp_refsetreset(sprefset *s) {
	s->used = 0;
}

#endif
