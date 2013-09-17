
/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

#include <sp.h>

static inline int sp_iensure(spi *i) {
	if (splikely((i->icount + 1) < i->itop))
		return 0;
	i->itop *= 2;
	i->i = sp_realloc(i->a, i->i, i->itop * sizeof(spipage*));
	if (spunlikely(i->i == NULL))
		return -1;
	return 0;
}

static inline int
sp_ipagesize(spi *i) {
	return sizeof(spipage) + sizeof(spv*) * i->pagesize;
}

static inline spipage*
sp_ipagealloc(spi *i) {
	spipage *p = sp_malloc(i->a, sp_ipagesize(i));
	if (spunlikely(p == NULL))
		return NULL;
	p->count = 0;
	return p;
}

int sp_iinit(spi *i, spa *a, int pagesize, spcmpf cmp, void *cmparg)
{
	i->a = a;
	i->cmp = cmp;
	i->cmparg = cmparg;
	i->count = 0;
	i->pagesize = pagesize;
	/* start from 4 pages vector */
	i->itop = 2;
	i->icount = 1;
	i->i = NULL;
	int rc = sp_iensure(i);
	if (spunlikely(rc == -1))
		return -1;
	/* allocate first page */
	i->i[0] = sp_ipagealloc(i);
	if (spunlikely(i->i[0] == NULL)) {
		sp_free(i->a, i->i);
		i->i = NULL;
		return -1;
	}
	return 0;
}

void sp_ifree(spi *i)
{
	uint32_t p = 0;
	while (p < i->icount) {
		uint32_t k = 0;
		while (k < i->i[p]->count) {
			sp_free(i->a, i->i[p]->i[k]);
			k++;
		}
		sp_free(i->a, i->i[p]);
		p++;
	}
	sp_free(i->a, i->i);
	i->i = NULL;
}

int sp_itruncate(spi *i)
{
	sp_ifree(i);
	return sp_iinit(i, i->a, i->pagesize, i->cmp, i->cmparg);
}

static inline void*
sp_iminof(spi *i, spipage *p, char *rkey, int size, uint32_t *idx)
{
	register int min = 0;
	register int max = p->count - 1;
	while (max >= min) {
		register int mid = min + ((max - min) >> 1);
		register int rc =
			i->cmp(p->i[mid]->key,
			       p->i[mid]->size, rkey, size, i->cmparg);
		switch (rc) {
		case -1: min = mid + 1;
			continue;
		case  1: max = mid - 1;
			continue;
		default: *idx = mid;
			return p->i[mid];
		}
	}
	*idx = min;
	return NULL;
}

static inline void*
sp_imaxof(spi *i, spipage *p, char *rkey, int size, uint32_t *idx)
{
	register int min = 0;
	register int max = p->count - 1;
	while (max >= min) {
		register int mid = min + ((max - min) >> 1);
		register int rc =
			i->cmp(p->i[mid]->key,
			       p->i[mid]->size,
				   rkey, size, i->cmparg);
		switch (rc) {
		case -1: min = mid + 1;
			continue;
		case  1: max = mid - 1;
			continue;
		default: *idx = mid;
			return p->i[mid];
		}
	}
	*idx = max;
	return NULL;
}

static inline int
sp_ipagecmp(spi *i, spipage *p, char *rkey, int size)
{
	if (spunlikely(p->count == 0))
		return 0;
	register int l =
		i->cmp(p->i[0]->key, p->i[0]->size, rkey, size, i->cmparg);
	register int r =
		i->cmp(p->i[p->count-1]->key, p->i[p->count-1]->size,
	           rkey, size, i->cmparg);
	/* inside page range */
	if (l <= 0 && r >= 0)
		return 0;
	/* page min < key */
	if (l == -1)
		return -1;
	/* page max > key */
	assert(r == 1);
	return 1;
}

static inline spipage*
sp_ipageof(spi *i, char *rkey, int size, uint32_t *idx)
{
	register int min = 0;
	register int max = i->icount - 1;
	while (max >= min) {
		register int mid = min + ((max - min) >> 1);
		switch (sp_ipagecmp(i, i->i[mid], rkey, size)) {
		case -1: min = mid + 1;
			continue;
		case  1: max = mid - 1;
			continue;
		default:
			*idx = mid;
			return i->i[mid];
		}
	}
	*idx = min;
	return NULL;
}

int sp_isetorget(spi *i, spv *v, spii *old)
{
	/* 1. do binary search on the vector and find usable
	 * page for a key */
	spipage *p = i->i[0];
	uint32_t a = 0;
	if (splikely(i->icount > 1)) {
		p = sp_ipageof(i, v->key, v->size, &a);
		if (spunlikely(p == NULL)) {
			if (a >= i->icount)
				a = i->icount-1;
			p = i->i[a];
			assert(a < i->icount);
		}
		assert(p != NULL);
	}

	/* 2. if page is full, split it and insert new one:
	 * copy second half of the keys from first page to second */
	/* 2.1. insert page to vector, by moving pointers forward */
	if (spunlikely(p->count == i->pagesize)) {
		int rc = sp_iensure(i);
		if (spunlikely(rc == -1))
			return -1;
		/* split page */
		spipage *n = sp_ipagealloc(i);
		if (spunlikely(n == NULL))
			return -1;
		int half = p->count >> 1;
		memcpy(&n->i[0], &p->i[half], sizeof(void*) * (half));
		n->count = half;
		p->count = half;
		/* split page i and insert new page */
		memmove(&i->i[a + 1], &i->i[a], sizeof(spipage*) * (i->icount - a));
		i->i[a] = p;
		i->i[a+1] = n;
		i->icount++;
		/* choose which part to use */
		if (sp_ipagecmp(i, n, v->key, v->size) <= 0) {
			p = n;
			a++;
		}
	}

	/* 3. if page is not full, do nothing */
	/* 4. do binary search on a page and match room, move
	 * pointers forward */
	/* 5. insert key, increment counters */
	assert(p->count < i->pagesize);

	uint32_t j;
	void *o = sp_iminof(i, p, v->key, v->size, &j);
	if (spunlikely(o)) {
		old->i = i;
		old->p = a;
		old->n = j;
		assert(sp_ival(old) == o);
		return 1;
	}
	if (j >= p->count) {
		p->i[p->count] = v;
	} else {
		memmove(&p->i[j + 1], &p->i[j], sizeof(spv*) * (p->count - j));
		p->i[j] = v;
	}
	i->count++;
	p->count++;
	return 0;
}

int sp_idelraw(spi *i, char *rkey, int size, spv **old)
{
	spipage *p = i->i[0];
	uint32_t a = 0;
	if (splikely(i->icount > 1))
		p = sp_ipageof(i, rkey, size, &a);
	if (spunlikely(p == NULL))
		return 0;
	uint32_t j;
	*old = sp_iminof(i, p, rkey, size, &j);
	if (spunlikely(*old == NULL))
		return 0;
	if (splikely(j != (uint32_t)(p->count-1)))
		memmove(&p->i[j], &p->i[j + 1], sizeof(spv*) * (p->count - j - 1));
	p->count--;
	i->count--;
	if (splikely(p->count > 0))
		return 1;
	/* do not touch last page */
	if (spunlikely(i->icount == 1))
		return 1;
	/* remove empty page */
	sp_free(i->a, i->i[a]);
	if (splikely(a != (i->icount - 1)))
		memmove(&i->i[a], &i->i[a + 1], sizeof(spipage*) * (i->icount - a - 1));
	i->icount--;
	return 1;
}

spv *sp_igetraw(spi *i, char *rkey, int size)
{
	spipage *p = i->i[0];
	uint32_t a = 0;
	if (splikely(i->icount > 1))
		p = sp_ipageof(i, rkey, size, &a);
	if (p == NULL)
		return NULL;
	uint32_t j;
	return sp_iminof(i, p, rkey, size, &j);
}

static inline int
sp_iworldcmp(spi *i, char *rkey, int size)
{
	register spipage *last = i->i[i->icount-1];
	register int l =
		i->cmp(i->i[0]->i[0]->key,
		       i->i[0]->i[0]->size, rkey, size, i->cmparg);
	register int r =
		i->cmp(last->i[last->count-1]->key,
		       last->i[last->count-1]->size,
	           rkey, size, i->cmparg);
	/* inside index range */
	if (l <= 0 && r >= 0)
		return 0;
	/* index min < key */
	if (l == -1)
		return -1;
	/* index max > key */
	assert(r == 1);
	return 1;
}

int sp_ilte(spi *i, spii *ii, char *k, int size)
{
	if (spunlikely(i->count == 0)) {
		sp_iinv(i, ii);
		return 0;
	}
	spipage *p = i->i[0];
	uint32_t a = 0;
	if (splikely(i->icount > 1))
		p = sp_ipageof(i, k, size, &a);
	if (p == NULL) {
		switch (sp_iworldcmp(i, k, size)) {
		case -1:
			sp_iinv(i, ii);
			break;
		case  1:
			ii->i = i;
			ii->p = i->icount - 1;
			ii->n = i->i[i->icount - 1]->count - 1;
			break;
		case  0:
			assert(0);
		}
		return 0;
	}
	uint32_t j;
	int eq = sp_iminof(i, p, k, size, &j) != NULL;
	ii->i = i;
	ii->p = a;
	ii->n = j;
	return eq;
}

int sp_igte(spi *i, spii *ii, char *k, int size)
{
	if (spunlikely(i->count == 0)) {
		sp_iinv(i, ii);
		return 0;
	}
	spipage *p = i->i[0];
	uint32_t a = 0;
	if (splikely(i->icount > 1))
		p = sp_ipageof(i, k, size, &a);
	if (p == NULL) {
		switch (sp_iworldcmp(i, k, size)) {
		case -1:
			ii->i = i;
			ii->p = i->icount - 1;
			ii->n = i->i[i->icount - 1]->count - 1;
			break;
		case  1:
			sp_iinv(i, ii);
			break;
		case  0:
			assert(0);
		}
		return 0;
	}
	uint32_t j;
	int eq = sp_imaxof(i, p, k, size, &j) != NULL;
	ii->i = i;
	ii->p = a;
	ii->n = j;
	return eq;
}
