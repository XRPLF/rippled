#ifndef SP_I_H_
#define SP_I_H_

/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

typedef struct spipage spipage;
typedef struct spi spi;
typedef struct spii spii;

struct spipage {
	uint16_t count;
	spv *i[];
} sppacked;

struct spi {
	spa *a;
	int pagesize;
	spipage **i;
	uint32_t itop;
	uint32_t icount;
	uint32_t count;
	spcmpf cmp;
	void *cmparg;
};

struct spii {
	spi *i;
	long long p, n;
};

int sp_iinit(spi*, spa*, int, spcmpf, void*);
void sp_ifree(spi*);
int sp_itruncate(spi*);
int sp_isetorget(spi *i, spv*, spii*);
int sp_idelraw(spi*, char*, int, spv**);
spv *sp_igetraw(spi*, char*, int);

static inline int
sp_idel(spi *i, spv *v, spv **old) {
	return sp_idelraw(i, v->key, v->size, old);
}

static inline spv*
sp_iget(spi *i, spv *v) {
	return sp_igetraw(i, v->key, v->size);
}

static inline void*
sp_imax(spi *i) {
	if (spunlikely(i->count == 0))
		return NULL;
	return i->i[i->icount-1]->i[i->i[i->icount-1]->count-1];
}

static inline void
sp_ifirst(spii *it) {
	it->p = 0;
	it->n = 0;
}

static inline void
sp_ilast(spii *it) {
	it->p = it->i->icount - 1;
	it->n = it->i->i[it->i->icount - 1]->count - 1;
}

static inline void
sp_iopen(spii *it, spi *i) {
	it->i = i;
	sp_ifirst(it);
}

static inline int
sp_ihas(spii *it) {
	return (it->p >= 0 && it->n >= 0) &&
	       (it->p < it->i->icount) &&
	       (it->n < it->i->i[it->p]->count);
}

static inline void
sp_ivalset(spii *it, spv *v) {
	it->i->i[it->p]->i[it->n] = v;
}

static inline spv*
sp_ival(spii *it) {
	if (spunlikely(! sp_ihas(it)))
		return NULL;
	return it->i->i[it->p]->i[it->n];
}

static inline int
sp_inext(spii *it) {
	if (spunlikely(! sp_ihas(it)))
		return 0;
	it->n++;
	while (it->p < it->i->icount) {
		spipage *p = it->i->i[it->p];
		if (spunlikely(it->n >= p->count)) {
			it->p++;
			it->n = 0;
			continue;
		}
		return 1;
	}
	return 0;
}

static inline int
sp_iprev(spii *it) {
	if (spunlikely(! sp_ihas(it)))
		return 0;
	it->n--;
	while (it->p >= 0) {
		if (spunlikely(it->n < 0)) {
			if (it->p == 0)
				return 0;
			it->p--;
			it->n = it->i->i[it->p]->count-1;
			continue;
		}
		return 1;
	}
	return 0;
}

static inline void
sp_iinv(spi *i, spii *ii) {
	ii->i = i;
	ii->p = -1;
	ii->n = -1;
}

int sp_ilte(spi*, spii*, char*, int);
int sp_igte(spi*, spii*, char*, int);

static inline int
sp_iset(spi *i, spv *v, spv **old)
{
	spii pos;
	int rc = sp_isetorget(i, v, &pos);
	if (splikely(rc <= 0))
		return rc;
	*old = sp_ival(&pos);
	sp_ivalset(&pos, v);
	return 1;
}

#endif
