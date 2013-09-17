
/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

#include <sp.h>

static inline void
sp_pageopen(spc *c, uint32_t page)
{
	assert(page < c->s->s.count);
	sppage *p = c->s->s.i[page];
	spepoch *e = p->epoch;
	c->pi = page;
	c->p = p;
	c->ph = (sppageh*)(e->db.map + p->offset);
	/* validate header */
	assert(c->ph->id > 0);
	c->pvi = 0 ;
	c->pv  = (spvh*)((char*)c->ph + sizeof(sppageh));
	c->dup = 0;
}

static inline void sp_pageclose(spc *c) {
	c->p  = NULL;
	c->ph = NULL;
	c->pv = NULL;
}

static inline void
sp_pagesetlast(spc *c)
{
	c->pvi = c->ph->count - 1;
	c->pv  = (spvh*)((char*)c->ph + sizeof(sppageh) + c->ph->bsize * c->pvi);
}

static inline int
sp_pageseek(spc *c, char *rkey, int size, int *minp, int *midp, int *maxp)
{
	int min = 0;
	int max = c->ph->count - 1;
	int mid = 0;
	while (max >= min)
	{
		mid = min + ((max - min) >> 1);
		spvh *a = (spvh*)((char*)c->ph + sizeof(sppageh) + c->ph->bsize * mid);
		register int rc =
			c->s->e->cmp(a->key, a->size, rkey, size, c->s->e->cmparg);
		switch (rc) {
		case -1: min = mid + 1;
			continue;
		case  1: max = mid - 1;
			continue;
		default:
			*minp = min;
			*midp = mid;
			*maxp = max;
			return 1;
		}
	}
	*minp = min;
	*midp = mid;
	*maxp = max;
	return 0;
}

static inline int
sp_pagesetlte(spc *c, char *rkey, int size)
{
	int mid, min, max;
	int match;
	int eq = 0;
	if (sp_pageseek(c, rkey, size, &min, &mid, &max)) {
		eq = 1;
		match = mid;
	} else {
		match = min;
	}
	/* not found, set minimal */
	if (match >= c->ph->count) {
		c->pv  = (spvh*)((char*)c->ph + sizeof(sppageh));
		c->pvi = 0;
	} else {
		c->pv  = (spvh*)((char*)c->ph + sizeof(sppageh) + c->ph->bsize * match);
		c->pvi = match;
	}
	return eq;
}

static inline int
sp_pagesetgte(spc *c, char *rkey, int size)
{
	int mid, min, max;
	int match;
	int eq = 0;
	if (sp_pageseek(c, rkey, size, &min, &mid, &max)) {
		eq = 1;
		match = mid;
	} else {
		match = max;
	}
	/* not found, set max */
	if (match >= c->ph->count)
		match = c->ph->count - 1;
	c->pv  = (spvh*)((char*)c->ph + sizeof(sppageh) + c->ph->bsize * match);
	c->pvi = match;
	return eq;
}

static inline void
sp_pagenext(spc *c)
{
	c->pvi++;
	c->pv = (spvh*)((char*)c->pv + c->ph->bsize);
	if (spunlikely(c->pvi == c->ph->count)) {
		sp_pageclose(c);
		uint32_t next_page = c->pi + 1;
		if (splikely(next_page < c->s->s.count))
			sp_pageopen(c, next_page);
	}
}

static inline void
sp_pageprev(spc *c)
{
	c->pvi--;
	c->pv = (spvh*)((char*)c->pv - c->ph->bsize);
	if (spunlikely(c->pvi < 0)) {
		sp_pageclose(c);
		int prev_page = c->pi - 1;
		if (splikely(prev_page >= 0)) {
			sp_pageopen(c, prev_page);
			sp_pagesetlast(c);
		}
	}
}

static inline void sp_first(spc *c)
{
	sp_iopen(&c->i0, c->s->i);
	if (! c->s->iskip)
		sp_iopen(&c->i1, sp_ipair(c->s));
	if (spunlikely(c->s->s.count == 0))
		return;
	sp_pageopen(c, 0);
}

static inline int
sp_firstkey(spc *c, char *rkey, int size)
{
	/* do lte search on all indexes for the key */
	int eq = sp_ilte(c->s->i, &c->i0, rkey, size);
	if (! c->s->iskip)
		eq = eq + sp_ilte(sp_ipair(c->s), &c->i1, rkey, size);
	/* match page for the key */
	if (spunlikely(c->s->s.count == 0))
		return eq;
	/* open page and do lte search of key */
	uint32_t idx = 0;
	sppage *p spunused;
	p = sp_catroute(&c->s->s, rkey, size, &idx);
	assert(p != NULL);
	sp_pageopen(c, idx);
	eq = eq + sp_pagesetlte(c, rkey, size);
	return eq;
}

static inline void sp_last(spc *c)
{
	sp_iopen(&c->i0, c->s->i);
	sp_ilast(&c->i0);
	if (! c->s->iskip) {
		sp_iopen(&c->i1, sp_ipair(c->s));
		sp_ilast(&c->i1);
	}
	if (spunlikely(c->s->s.count == 0))
		return;
	sp_pageopen(c, c->s->s.count-1);
	sp_pagesetlast(c);
}

static inline int sp_lastkey(spc *c, char *rkey, int size)
{
	/* do gte search the index for the key */
	int eq = sp_igte(c->s->i, &c->i0, rkey, size);
	if (! c->s->iskip)
		eq = eq + sp_igte(sp_ipair(c->s), &c->i1, rkey, size);
	/* match page for the key */
	if (spunlikely(c->s->s.count == 0))
		return eq;
	/* open page and do gte search of key */
	uint32_t idx = 0;
	sppage *p spunused;
	p = sp_catroute(&c->s->s, rkey, size, &idx);
	assert(p != NULL);
	sp_pageopen(c, idx);
	eq = eq + sp_pagesetgte(c, rkey, size);
	return eq;
}

void sp_cursoropen(spc *c, sp *s, sporder o, char *rkey, int size)
{
	/* lock all */
	sp_glock(s);

	c->m = SPMCUR;
	c->o = o;
	c->s = s;
	c->dup = 0;
	c->p = NULL;
	c->ph = NULL;
	c->pi = 0;
	c->pvi = 0;
	c->pv = NULL;
	c->vsrc = SPCNONE;

	sp_iinv(c->s->i, &c->i0);
	sp_iinv(sp_ipair(c->s), &c->i1);

	c->r.type = SPREFNONE;
	switch (o) {
	case SPGTE:
	case SPGT:
		if (rkey) {
			/* init first key and skip on gt */
			if (sp_firstkey(c, rkey, size) && o == SPGT)
				sp_iterate(c);
			return;
		}
		sp_first(c);
		break;
	case SPLTE:
	case SPLT:
		if (rkey) {
			/* init last key and skip on lt */
			if (sp_lastkey(c, rkey, size) && o == SPLT)
				sp_iterate(c);
			return;
		}
		sp_last(c);
		break;
	}
}

void sp_cursorclose(spc *c)
{
	/* unlock all */
	sp_gunlock(c->s);
}

static inline void sp_reset(spc *c) {
	c->vsrc = SPCNONE;
	c->r.type = SPREFNONE;
	c->dup = 0;
}

static inline int sp_next(spc *c)
{
	switch (c->vsrc) {
	case SPCI0:
		sp_inext(&c->i0);
		if (c->dup & SPCPDUP)
			sp_pagenext(c);
		if (c->dup & SPCVDUP)
			sp_inext(&c->i1);
		break;
	case SPCI1:
		sp_inext(&c->i1);
		if (c->dup & SPCPDUP)
			sp_pagenext(c);
		if (c->dup & SPCVDUP)
			sp_inext(&c->i0);
		break;
	case SPCP:
		assert(c->p != NULL);
		assert(c->r.v.vh == c->pv);
		sp_pagenext(c);
		break;
	case SPCNONE:
		break;
	}
	sp_reset(c);
	
	int rc;
	spcsrc src = SPCNONE;
	spv *v0 = sp_ival(&c->i0);
	spv *v1 = sp_ival(&c->i1);
	spv *v  = NULL;

	/* END */
	if (v0 == NULL && v1 == NULL && c->pv == NULL) {
		c->vsrc = SPCNONE;
		return 0;
	}

	if (v0 && v1) {
		rc = c->s->e->cmp(v0->key, v0->size, v1->key, v1->size,
						  c->s->e->cmparg);
		switch (rc) {
		case  0: c->dup |= SPCVDUP;
		case -1: v = v0, src = SPCI0;
			break;
		case  1: v = v1, src = SPCI1;
			break;
		}
	} else if (v0) {
		v = v0, src = SPCI0;
	} else if (v1) {
		v = v1, src = SPCI1;
	}

	/* no page key */
	if (c->pv == NULL) {
		c->vsrc = src;
		c->r.type = SPREFM;
		c->r.v.v = v; 
		return 1;
	}
	/* no index key */
	if (v == NULL) {
		c->vsrc = SPCP;
		c->r.type = SPREFD;
		c->r.v.vh = c->pv;
		return 1;
	}

	/* compare in-memory key and page one's */
	rc = c->s->e->cmp(v->key, v->size, c->pv->key, c->pv->size,
	                  c->s->e->cmparg);
	switch (rc) {
	case 0: c->dup |= SPCPDUP;
	case -1:
		c->vsrc = src;
		c->r.type = SPREFM;
		c->r.v.v = v; 
		break;
	case 1:
		c->vsrc = SPCP;
		c->r.type = SPREFD;
		c->r.v.vh = c->pv;
		break;
	}
	return 1;
}

static inline int sp_prev(spc *c)
{
	switch (c->vsrc) {
	case SPCI0:
		sp_iprev(&c->i0);
		if (c->dup & SPCPDUP)
			sp_pageprev(c);
		if (c->dup & SPCVDUP)
			sp_iprev(&c->i1);
		break;
	case SPCI1:
		sp_iprev(&c->i1);
		if (c->dup & SPCPDUP)
			sp_pageprev(c);
		if (c->dup & SPCVDUP)
			sp_iprev(&c->i0);
		break;
	case SPCP:
		assert(c->p != NULL);
		assert(c->r.v.vh == c->pv);
		sp_pageprev(c);
		break;
	case SPCNONE:
		break;
	}
	sp_reset(c);
	
	int rc;
	spcsrc src = SPCNONE;
	spv *v0 = sp_ival(&c->i0);
	spv *v1 = sp_ival(&c->i1);
	spv *v  = NULL;

	/* END */
	if (v0 == NULL && v1 == NULL && c->pv == NULL) {
		c->vsrc = SPCNONE;
		return 0;
	}

	if (v0 && v1) {
		rc = c->s->e->cmp(v0->key, v0->size, v1->key, v1->size,
						  c->s->e->cmparg);
		switch (rc) {
		case  0: c->dup |= SPCVDUP;
		case -1: v = v1, src = SPCI1;
			break;
		case  1: v = v0, src = SPCI0;
			break;
		}
	} else if (v0) {
		v = v0, src = SPCI0;
	} else if (v1) {
		v = v1, src = SPCI1;
	}

	/* no page key */
	if (c->pv == NULL) {
		c->vsrc = src;
		c->r.type = SPREFM;
		c->r.v.v = v; 
		return 1;
	}
	/* no index key */
	if (v == NULL) {
		c->vsrc = SPCP;
		c->r.type = SPREFD;
		c->r.v.vh = c->pv;
		return 1;
	}

	/* compare in-memory key and page one's */
	rc = c->s->e->cmp(v->key, v->size, c->pv->key, c->pv->size,
	                  c->s->e->cmparg);
	switch (rc) {
	case  0: c->dup |= SPCPDUP;
	case  1:
		c->vsrc = src;
		c->r.type = SPREFM;
		c->r.v.v = v; 
		break;
	case -1:
		c->vsrc = SPCP;
		c->r.type = SPREFD;
		c->r.v.vh = c->pv;
		break;
	}
	return 1;
}

int sp_iterate(spc *c) {
	int rc = 0;
	do {
		switch (c->o) {
		case SPGTE:
		case SPGT: rc = sp_next(c);
			break;
		case SPLTE:
		case SPLT: rc = sp_prev(c);
			break;
		}
	} while (rc > 0 && sp_refisdel(&c->r));
	
	return rc;
}

static inline int
sp_matchi(sp *s, spi *i, void *key, size_t size, void **v, size_t *vsize)
{
	spv *a = sp_igetraw(i, key, size);
	if (splikely(a)) {
		if (a->flags & SPDEL)
			return 0;
		if (v) {
			*vsize = sp_vvsize(a);
			*v = NULL;
			if (*vsize > 0) {
				*v = sp_memdup(s, sp_vv(a), sp_vvsize(a));
				if (spunlikely(*v == NULL))
					return sp_e(s, SPEOOM, "failed to allocate key");
			}
		}
		return 1;
	}
	return 0;
}

int sp_match(sp *s, void *k, size_t ksize, void **v, size_t *vsize)
{
	/* I. match both in-memory index'es for the key */
	sp_lock(&s->locki);
	int rc = sp_matchi(s, s->i, k, ksize, v, vsize);
	if (rc == -1 || rc == 1) {
		sp_unlock(&s->locki);
		return rc;
	}
	/*
	 * skip the second index if it is been truncating at the
	 * somement, all updates are on disk pages.
	*/
	if (! s->iskip) {
		int rc = sp_matchi(s, sp_ipair(s), k, ksize, v, vsize);
		if (rc == -1 || rc == 1) {
			sp_unlock(&s->locki);
			return rc;
		}
	}
	sp_unlock(&s->locki);

	/* II. match the page */
	sp_lock(&s->locks);
	uint32_t page = 0;
	sppage *p = sp_catfind(&s->s, k, ksize, &page);
	if (p == NULL) {
		sp_unlock(&s->locks);
		return 0;
	}

	/* III. match the key in the page */
	spepoch *e = (spepoch*)p->epoch;
	sp_lock(&e->lock);

	sppageh *ph = (sppageh*)(e->db.map + p->offset);
	rc = 0;
	register uint32_t min = 0;
	register uint32_t max = ph->count - 1;
	spvh *match = NULL;
	while (max >= min) {
		register uint32_t mid = min + ((max - min) >> 1);
		register spvh *v =
			(spvh*)(e->db.map + p->offset + sizeof(sppageh) +
			        ph->bsize * mid);
		switch (s->e->cmp(v->key, v->size, k, ksize, s->e->cmparg)) {
		case -1: min = mid + 1;
			continue;
		case  1: max = mid - 1;
			continue;
		default:
			match = v;
			goto done;
		}
	}
done:
	if (match == NULL)
		goto ret;
	if (v) {
		*vsize = match->vsize;
		*v = NULL;
		if (match->vsize > 0) {
			*v = sp_memdup(s, e->db.map + p->offset + match->voffset, match->vsize);
			if (spunlikely(*v == NULL)) {
				sp_e(s, SPEOOM, "failed to allocate key");
				rc = -1;
				goto ret;
			}
		}
	}
	rc = 1;
ret:
	sp_unlock(&e->lock);
	sp_unlock(&s->locks);
	return rc;
}
