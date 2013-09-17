#ifndef SP_CORE_H_
#define SP_CORE_H_

/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

#define SP_VERSION_MAJOR 1
#define SP_VERSION_MINOR 1

typedef struct sp sp;
typedef struct spenv spenv;

enum spmagic {
	SPMCUR  = 0x15481936L,
	SPMENV  = 0x06154834L,
	SPMDB   = 0x00fec0feL,
	SPMNONE = 0L
};

typedef enum spmagic spmagic;

struct spenv {
	spmagic m;
	spe e;
	int inuse;
	spallocf alloc;
	void *allocarg;
	spcmpf cmp;
	void *cmparg;
	uint32_t flags;
	char *dir;
	int merge;
	uint32_t mergewm;
	uint32_t page;
	uint32_t dbnewsize;
	float dbgrow;
	int gc;
	float gcfactor;
};

struct sp {
	spmagic m;
	spenv *e;
	spa a;
	sprep rep;
	spi *i, i0, i1;
	int iskip;          /* skip second index during read */
	uint64_t psn;       /* page sequence number */
	spcat s;
	volatile int stop;
	sptask merger;
	sprefset refs;      /* pre allocated key buffer (page merge) */
	int lockc;          /* incremental cursor lock */
	spspinlock lockr;   /* repository lock */
	spspinlock locks;   /* space lock */
	spspinlock locki;   /* index lock */
};

int sp_rotate(sp*);

static inline int sp_active(sp *s) {
	return !s->stop;
}

static inline int
sp_e(sp *s, int type, ...) {
	va_list args;
	va_start(args, type);
	sp_ve(&s->e->e, type, args);
	va_end(args);
	return -1;
}

static inline int
sp_ee(spenv *e, int type, ...) {
	va_list args;
	va_start(args, type);
	sp_ve(&e->e, type, args);
	va_end(args);
	return -1;
}

static inline void
sp_glock(sp *s) {
	if (s->lockc > 0)
		return;
	sp_lock(&s->lockr);
	sp_replockall(&s->rep);
	sp_lock(&s->locki);
	sp_lock(&s->locks);
	s->lockc++;
}

static inline void
sp_gunlock(sp *s) {
	s->lockc--;
	if (s->lockc > 0)
		return;
	sp_unlock(&s->locks);
	sp_unlock(&s->locki);
	sp_repunlockall(&s->rep);
	sp_unlock(&s->lockr);
}

static inline void
sp_iskipset(sp *s, int v) {
	sp_lock(&s->locki);
	s->iskip = v;
	sp_unlock(&s->locki);
}

static inline spi*
sp_ipair(sp *s) {
	return (s->i == &s->i0 ? &s->i1 : &s->i0);
}

static inline spi*
sp_iswap(sp *s) {
	spi *old = s->i;
	s->i = sp_ipair(s);
	return old;
}

#endif
