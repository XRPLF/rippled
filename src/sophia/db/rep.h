#ifndef SP_REP_H_
#define SP_REP_H_

/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

typedef struct spepoch spepoch;
typedef struct sprep sprep;

enum spepochtype {
	SPUNDEF,
	SPLIVE,
	SPXFER,
	SPDB
};

typedef enum spepochtype spepochtype;

#define SPRNONE 0
#define SPRDB   1
#define SPRDBI  2
#define SPRLOG  4
#define SPRLOGI 8

struct spepoch {
	uint32_t epoch;
	uint32_t n;       /* count of pages */
	uint32_t ngc;     /* count of gc pages */
	uint32_t nupdate; /* count of updated keys */
	spepochtype type; /* epoch life-cycle state */
	uint32_t recover; /* recover status */
	spfile log, db;
	spspinlock lock;  /* db lock */
	splist pages;     /* list of associated pages */
	splist link;
};

struct sprep {
	spa *a;
	uint32_t epoch;
	splist l;
	int n;
	int ndb;
	int nxfer;
};

void sp_repinit(sprep*, spa*);
void sp_repfree(sprep*);
int sp_repprepare(sprep*);
spepoch *sp_repmatch(sprep *r, uint32_t epoch);
spepoch *sp_repalloc(sprep*, uint32_t);
void sp_repattach(sprep*, spepoch*);
void sp_repdetach(sprep*, spepoch*);
void sp_repset(sprep*, spepoch*, spepochtype);

static inline uint32_t
sp_repepoch(sprep *r) {
	return r->epoch;
}

static inline void
sp_repepochincrement(sprep *r) {
	r->epoch++;
}

static inline void
sp_replockall(sprep *r) {
	register splist *i;
	sp_listforeach(&r->l, i) {
		register spepoch *e = spcast(i, spepoch, link);
		sp_lock(&e->lock);
	}
}

static inline void
sp_repunlockall(sprep *r) {
	register splist *i;
	sp_listforeach(&r->l, i) {
		register spepoch *e = spcast(i, spepoch, link);
		sp_unlock(&e->lock);
	}
}

static inline spepoch*
sp_replive(sprep *r) {
	register spepoch *e = spcast(r->l.prev, spepoch, link);
	assert(e->type == SPLIVE);
	return e;
}

static inline spepoch*
sp_repxfer(sprep *r) {
	register splist *i;
	sp_listforeach(&r->l, i) {
		register spepoch *s = spcast(i, spepoch, link);
		if (s->type == SPXFER)
			return s;
	}
	return NULL;
}

static inline spepoch*
sp_repgc(sprep *r, float factor) {
	register splist *i;
	sp_listforeach(&r->l, i) {
		register spepoch *s = spcast(i, spepoch, link);
		if (s->type != SPDB)
			continue;
		if (s->ngc > (s->n * factor))
			return s;
	}
	return NULL;
}

#endif
