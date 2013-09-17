
/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

#include <sp.h>

static inline int sp_catensure(spcat *c) {
	if ((c->count + 1) < c->top)
		return 0;
	c->top *= 2;
	c->i = realloc(c->i, c->top * sizeof(sppage*));
	if (c->i == NULL)
		return -1;
	return 0;
}

int sp_catinit(spcat *c, spa *a, int top, spcmpf cmp, void *cmparg) {
	c->a = a;
	c->cmp = cmp;
	c->cmparg = cmparg;
	c->count = 0;
	c->top = top;
	c->i = sp_malloc(a, sizeof(sppage*) * top);
	if (spunlikely(c->i == NULL))
		return -1;
	return 0;
}

void sp_catfree(spcat *c) {
	uint32_t p = 0;
	while (p < c->count) {
		sp_free(c->a, c->i[p]->min);
		sp_free(c->a, c->i[p]->max);
		sp_free(c->a, c->i[p]);
		p++;
	}
	sp_free(c->a, c->i);
}

static inline int
cmppage(spcat *c, sppage *p, sppage *v) {
	int l = c->cmp(p->min->key,
	               p->min->size,
	               v->min->key,
	               v->min->size, c->cmparg);
	assert(l == c->cmp(p->max->key,
	                   p->max->size,
	                   v->max->key,
	                   v->max->size, c->cmparg));
	return l;
}

static inline sppage*
sp_catsearch(spcat *c, sppage *v, uint32_t *index) {
	int min = 0;
	int max = c->count - 1;
	while (max >= min) {
		int mid = min + ((max - min) >> 1);
		switch (cmppage(c, c->i[mid], v)) {
		case -1: min = mid + 1;
			continue;
		case  1: max = mid - 1;
			continue;
		default:
			*index = mid;
			return c->i[mid];
		}
	}
	*index = min;
	return NULL;
}

int sp_catset(spcat *c, sppage *n, sppage **o)
{
	uint32_t i;
	sppage *p = sp_catsearch(c, n, &i);
	if (p) {
		/* replace */
		*o = c->i[i];
		c->i[i] = p;
		return 0;
	}
	/* insert */
	int rc = sp_catensure(c);
	if (spunlikely(rc == -1))
		return -1;
	/* split page index and insert new page */
	memmove(&c->i[i + 1], &c->i[i], sizeof(sppage*) * (c->count - i));
	c->i[i] = n;
	c->count++;
	*o = NULL;
	return 0;
}

int sp_catdel(spcat *c, uint32_t idx)
{
	assert(idx < c->count);
	if (splikely(idx != (uint32_t)(c->count-1)))
		memmove(&c->i[idx], &c->i[idx + 1],
		        sizeof(sppage*) * (c->count - idx - 1));
	c->count--;
	return 0;
}

static inline int
cmpkey(spcat *c, sppage *p, void *rkey, int size)
{
	register int l =
		c->cmp(p->min->key, p->min->size, rkey, size, c->cmparg);
	register int r =
		c->cmp(p->max->key, p->max->size, rkey, size, c->cmparg);
	/* inside page range */
	if (l <= 0 && r >= 0)
		return 0;
	/* key > page */
	if (l == -1)
		return -1;
	/* key < page */
	assert(r == 1);
	return 1;
}

sppage*
sp_catfind(spcat *c, char *rkey, int size, uint32_t *index)
{
	register int min = 0;
	register int max = c->count - 1;
	while (max >= min) {
		register int mid = min + ((max - min) >> 1);
		switch (cmpkey(c, c->i[mid], rkey, size)) {
		case -1: min = mid + 1;
			continue;
		case  1: max = mid - 1;
			continue;
		default: *index = mid;
			return c->i[mid];
		}
	}
	*index = min;
	return NULL;
}

sppage *sp_catroute(spcat *c, char *rkey, int size, uint32_t *idx)
{
	if (spunlikely(c->count == 1))
		return c->i[0];
	uint32_t i;
	sppage *p = sp_catfind(c, rkey, size, &i);
	if (splikely(p)) {
		*idx = i;
		return p;
	}
	if (spunlikely(i >= c->count))
		i = c->count - 1;

	if (i > 0 && c->cmp(c->i[i]->min->key, c->i[i]->min->size,
	                    rkey,
	                    size, c->cmparg) == 1) {
		i = i - 1;
	}
	if (idx)
		*idx = i;
	return c->i[i];
}

int sp_catown(spcat *c, uint32_t idx, spv *v)
{
	register sppage *p = c->i[idx];
	/* equal or equal min or equal max */
	switch (cmpkey(c, p, v->key, v->size)) {
	case  0:
		return 1;
	case -1: /* key > page */
		/* key > max */
		if (idx == c->count-1)
			return 1;
		break;
	case  1: /* key < page */
		/* key < min */
		if (idx == 0)
			return 1;
		break;
	}
	/* key > page && key < page+1.min */
	if (c->cmp(v->key, v->size,
	           c->i[idx + 1]->min->key,
	           c->i[idx + 1]->min->size, c->cmparg) == -1)
		return 1;
	return 0;
}
