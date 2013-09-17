
/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

#include <sp.h>

typedef struct {
	uint32_t count;
	uint32_t psize;
	uint32_t bsize;
} spupdate0;

static inline void
sp_mergeget0(spii *pos, uint32_t n, spupdate0 *u)
{
	memset(u, 0, sizeof(*u));
	/* 
	 * collect n or less versions for scheduled page write,
	 * not marked as delete, calculate page size and the
	 * block size.
	*/
	spii i = *pos;
	while (u->count < n && sp_ihas(&i)) {
		spv *v = sp_ival(&i);
		if (v->flags & SPDEL) {
			sp_inext(&i);
			continue;
		}
		if (v->size > u->bsize)
			u->bsize = v->size;
		sp_inext(&i);
		u->count++;
		u->psize += sp_vvsize(v);
	}
	u->bsize += sizeof(spvh);
	u->psize += sizeof(sppageh) + u->bsize * u->count;
}

static inline int sp_merge0(sp *s, spepoch *x, spi *index)
{
	spv *max = NULL;
	spv *min = NULL;
	int rc;
	spii i;
	sp_iopen(&i, index);

	while (sp_active(s))
	{
		/* get the new page properties and a data */
		spupdate0 u;
		sp_mergeget0(&i, s->e->page, &u);
		if (spunlikely(u.count == 0))
			 break;

		/* ensure enough space for the page in the file */
		sp_lock(&x->lock);
		rc = sp_mapensure(&x->db, u.psize, s->e->dbgrow);
		if (spunlikely(rc == -1)) {
			sp_unlock(&x->lock);
			sp_e(s, SPEIO, "failed to remap db file", x->epoch);
			goto err;
		}
		sp_unlock(&x->lock);

		/* write the page.
		 *
		 * [header] [keys (block sized)] [values]
		 *
		 * Use partly precalculated crc for a version. 
		*/
		sppageh *h = (sppageh*)(x->db.map + x->db.used);
		h->id    = ++s->psn;
		h->count = u.count;
		h->bsize = u.bsize;
		h->size  = u.psize - sizeof(sppageh);
		h->crc   = sp_crc32c(0, &h->id, sizeof(sppageh) - sizeof(h->crc));

		char *ph = x->db.map + x->db.used + sizeof(sppageh);
		char *pv = ph + u.count * u.bsize;

		uint32_t current = 0;
		spv *last = NULL;
		while (sp_active(s) && current < u.count)
		{
			spv *v = sp_ival(&i);
			if (v->flags & SPDEL) {
				sp_inext(&i);
				continue;
			}
			if (spunlikely(min == NULL)) {
				min = sp_vdup(s, v);
				if (spunlikely(min == NULL)) {
					sp_e(s, SPEOOM, "failed to allocate key");
					goto err;
				}
			}
			assert(v->size <= u.bsize);
			spvh *vh = (spvh*)(ph);
			vh->size    = v->size;
			vh->flags   = v->flags;
			vh->vsize   = sp_vvsize(v);
			vh->voffset = pv - (char*)h;
			vh->crc     = sp_crc32c(v->crc, &vh->size, sizeof(spvh) - sizeof(vh->crc));
			memcpy(vh->key, v->key, v->size);
			memcpy(pv, sp_vv(v), vh->vsize);

			ph += u.bsize;
			pv += vh->vsize;
			last = v;
			current++;
			sp_inext(&i);
		}

		/* cancellation point check */
		if (! sp_active(s))
			goto err;

		/* create in-memory page */
		sppage *page = sp_pagenew(s, x);
		if (spunlikely(page == NULL)) {
			sp_e(s, SPEOOM, "failed to allocate page");
			goto err;
		}
		max = sp_vdup(s, last);
		if (spunlikely(max == NULL)) {
			sp_e(s, SPEOOM, "failed to allocate key");
			goto err;
		}
		assert(min != NULL);
		page->id     = s->psn;
		page->offset = x->db.used;
		page->size   = u.psize;
		page->min    = min;
		page->max    = max;

		/* insert page to the index */
		sp_lock(&s->locks);
		sppage *o = NULL;
		rc = sp_catset(&s->s, page, &o);
		if (spunlikely(rc == -1)) {
			sp_unlock(&s->locks);
			sp_pagefree(s, page);
			sp_e(s, SPEOOM, "failed to allocate page index page");
			goto err;
		}
		sp_unlock(&s->locks);

		/* attach page to the epoch list */
		sp_pageattach(page);

		/* advance file buffer */
		sp_mapuse(&x->db, u.psize);

		min = NULL;
		max = NULL;
	}
	return 0;
err:
	if (min)
		sp_free(&s->a, min);
	if (max)
		sp_free(&s->a, max);
	return -1;
}

typedef struct {
	uint32_t pi;
	sppage *p;
	spepoch *s; /* p->epoch */
	uint32_t count;
	uint32_t bsize;
} spupdate;

typedef struct {
	/* a is an original page version
	   b is in-memory version */
	int a_bsize, b_bsize;
	int a_count, b_count;
	int A, B;
	spvh *a;
	spv *b;
	spref last;
	spii i;
	spepoch *x;
} spmerge;

typedef struct {
	splist split;
	int count;
} spsplit;

static inline int
sp_mergeget(sp *s, spii *from, spupdate *u)
{
	spii i = *from;
	if (spunlikely(! sp_ihas(&i)))
		return 0;
	memset(u, 0, sizeof(spupdate));
	/* match the origin page and a associated
	 * range of keys. */
	sppage *origin = NULL;
	uint32_t origin_idx = 0;
	uint32_t n = 0;
	while (sp_ihas(&i)) {
		spv *v = sp_ival(&i);
		if (splikely(origin)) {
			if (! sp_catown(&s->s, origin_idx, v))
				break;
		}  else {
			origin = sp_catroute(&s->s, v->key, v->size, &origin_idx);
			assert(((spepoch*)origin->epoch)->type == SPDB);
		}
		if (v->size > u->bsize)
			u->bsize = v->size;
		sp_inext(&i);
		n++;
	}
	assert(n > 0);
	u->count = n;
	u->bsize += sizeof(spvh);
	u->pi = origin_idx;
	u->p = origin;
	u->s = origin->epoch;
	return 1;
}

static inline void
sp_mergeinit(spepoch *x, spmerge *m, spupdate *u, spii *from)
{
	sppageh *h = (sppageh*)(u->s->db.map + u->p->offset);
	uint32_t bsize = u->bsize;
	if (h->bsize > bsize)
		bsize = h->bsize;
	m->a_bsize = h->bsize;
	m->b_bsize = bsize;
	memset(&m->last, 0, sizeof(m->last));
	m->i = *from;
	m->A = 0;
	m->B = 0;
	m->a_count = h->count;
	m->b_count = u->count;
	m->a = (spvh*)((char*)h + sizeof(sppageh));
	m->b = sp_ival(from);
	m->x = x;
}

static inline int sp_mergenext(sp *s, spmerge *m)
{
	if (m->A < m->a_count && m->B < m->b_count)
	{
		register int cmp =
			s->e->cmp(m->a->key, m->a->size,
		              m->b->key,
		              m->b->size, s->e->cmparg);
		switch (cmp) {
		case  0:
			/* use updated key B */
			m->last.type = SPREFM;
			m->last.v.v = m->b;
			m->A++;
			m->a = (spvh*)((char*)m->a + m->a_bsize);
			m->B++;
			sp_inext(&m->i);
			m->b = sp_ival(&m->i);
			return 1;
		case -1:
			/* use A */
			m->last.type = SPREFD;
			m->last.v.vh = m->a;
			m->A++;
			m->a = (spvh*)((char*)m->a + m->a_bsize);
			return 1;
		case  1:
			/* use B */
			m->last.type = SPREFM;
			m->last.v.v = m->b;
			m->B++;
			sp_inext(&m->i);
			m->b = sp_ival(&m->i);
			return 1;
		}
	}
	if (m->A < m->a_count) {
		/* use A */
		m->last.type = SPREFD;
		m->last.v.vh = m->a;
		m->A++;
		m->a = (spvh*)((char*)m->a + m->a_bsize);
		return 1;
	}
	if (m->B < m->b_count) {
		/* use B */
		m->last.type = SPREFM;
		m->last.v.v = m->b;
		m->B++;
		sp_inext(&m->i);
		m->b = sp_ival(&m->i);
		return 1;
	}
	return 0;
}

static inline void
sp_splitinit(spsplit *l) {
	sp_listinit(&l->split);
	l->count = 0;
}

static inline void
sp_splitfree(sp *s, spsplit *l) {
	splist *i, *n;
	sp_listforeach_safe(&l->split, i, n) {
		sppage *p = spcast(i, sppage, link);
		sp_pagefree(s, p);
	}
}

static inline int sp_split(sp *s, spupdate *u, spmerge *m, spsplit *l)
{
	int rc;
	int bsize = m->b_bsize;
	uint32_t pagesize = sizeof(sppageh);
	uint32_t count = 0;
	/*
	 * merge in-memory keys with the origin page keys,
	 * skip any deletes and calculate result
	 * page size.
	*/
	sp_refsetreset(&s->refs);
	while (count < s->e->page && sp_mergenext(s, m)) {
		if (sp_refisdel(&m->last))
			continue;
		sp_refsetadd(&s->refs, &m->last);
		pagesize += bsize + sp_refvsize(&m->last);
		count++;
	}
	if (spunlikely(count == 0 && l->count > 0))
		return 0;

	/*
	 * set the origin page id for a first spitted page
	*/
	uint32_t psn = (l->count == 0) ? u->p->id : ++s->psn;

	/* ensure enough space for the page in the file */
	sp_lock(&m->x->lock);
	rc = sp_mapensure(&m->x->db, pagesize, s->e->dbgrow);
	if (spunlikely(rc == -1)) {
		sp_unlock(&m->x->lock);
		return sp_e(s, SPEIO, "failed to remap db file",
		            m->x->epoch);
	}
	sp_unlock(&m->x->lock);

	/* in case if all origin page keys are deleted.
	 *
	 * write special page header without any data, indicating
	 * that page should be skipped during recovery
	 * and not being added to the index.
	 */
	if (spunlikely(count == 0 && l->count == 0)) {
		sppageh *h = (sppageh*)(m->x->db.map + m->x->db.used);
		h->id    = psn;
		h->count = 0;
		h->bsize = 0;
		h->size  = 0;
		h->crc   = sp_crc32c(0, &h->id, sizeof(sppageh) - sizeof(h->crc));
		sp_mapuse(&m->x->db, pagesize);
		return 0;
	}

	spref *r = s->refs.r;
	spref *min = r;
	spref *max = r + (count - 1);

	/*
	 * write the page
	*/
	sppageh *h = (sppageh*)(m->x->db.map + m->x->db.used);
	h->id    = psn;
	h->count = count;
	h->bsize = bsize;
	h->size  = pagesize - sizeof(sppageh);
	h->crc   = sp_crc32c(0, &h->id, sizeof(sppageh) - sizeof(h->crc));

	spvh *ptr  = (spvh*)(m->x->db.map + m->x->db.used + sizeof(sppageh));
	char *ptrv = (char*)ptr + count * bsize;

	uint32_t i = 0;
	while (i < count)
	{
		uint32_t voffset = ptrv - (char*)h;
		switch (r->type) {
		case SPREFD:
			memcpy(ptr, r->v.vh, sizeof(spvh) + r->v.vh->size);
			memcpy(ptrv, u->s->db.map + u->p->offset + r->v.vh->voffset,
			       r->v.vh->vsize);
			ptr->voffset = voffset;
			uint32_t crc;
			crc = sp_crc32c(0, ptr->key, ptr->size);
			crc = sp_crc32c(crc, ptrv, r->v.vh->vsize);
			crc = sp_crc32c(crc, &ptr->size, sizeof(spvh) - sizeof(ptr->crc));
			ptr->crc = crc;
			ptrv += r->v.vh->vsize;
			break;
		case SPREFM:
			ptr->size    = r->v.v->size;
			ptr->flags   = r->v.v->flags;
			ptr->voffset = voffset;
			ptr->vsize   = sp_vvsize(r->v.v);
			ptr->crc = sp_crc32c(r->v.v->crc, &ptr->size, sizeof(spvh) -
			                     sizeof(ptr->crc));
			memcpy(ptr->key, r->v.v->key, r->v.v->size);
			memcpy(ptrv, sp_vv(r->v.v), ptr->vsize);
			ptrv += ptr->vsize;
			break;
		}
		assert((uint32_t)(ptrv - (char*)h) <= pagesize);
		ptr = (spvh*)((char*)ptr + bsize);
		r++;
		i++;
	}

	/* create in-memory page */
	sppage *p = sp_pagenew(s, m->x);
	if (spunlikely(p == NULL))
		return sp_e(s, SPEOOM, "failed to allocate page");
	p->id     = psn;
	p->offset = m->x->db.used;
	p->size   = pagesize;
	p->min    = sp_vdupref(s, min, m->x->epoch);
	if (spunlikely(p->min == NULL)) {
		sp_free(&s->a, p);
		return sp_e(s, SPEOOM, "failed to allocate key");
	}
	p->max    = sp_vdupref(s, max, m->x->epoch);
	if (spunlikely(p->max == NULL)) {
		sp_free(&s->a, p->min);
		sp_free(&s->a, p);
		return sp_e(s, SPEOOM, "failed to allocate key");
	}

	/* add page to split list */
	sp_listappend(&l->split, &p->link);
	l->count++;

	/* advance buffer */
	sp_mapuse(&m->x->db, pagesize);
	return 1;
}

static inline int sp_splitcommit(sp *s, spupdate *u, spmerge *m, spsplit *l)
{
	sp_lock(&s->locks);
	/* remove origin page, if there were no page
	 * updates after split */
	if (spunlikely(l->count == 0)) {
		sp_pagefree(s, u->p);
		u->s->ngc++;
		u->p = NULL;
		sp_catdel(&s->s, u->pi);
		sp_unlock(&s->locks);
		return 0;
	}
	splist *i, *n;
	sp_listforeach_safe(&l->split, i, n)
	{
		sppage *p = spcast(i, sppage, link);
		/* update origin page first */
		if (spunlikely(p->id == u->p->id)) {
			sp_listunlink(&p->link);
			/* relink origin page to new epoch */
			sppage *origin = u->p;
			assert(origin->epoch != m->x);
			sp_listunlink(&origin->link);
			u->s->ngc++; /* origin db epoch */
			m->x->n++; /* current db epoch */
			sp_listappend(&m->x->pages, &origin->link);
			/* update origin page */
			origin->offset = p->offset;
			assert(p->epoch == m->x);
			origin->epoch = m->x;
			origin->size  = p->size;
			sp_free(&s->a, origin->min);
			sp_free(&s->a, origin->max);
			origin->min   = p->min;
			origin->max   = p->max;
			sp_free(&s->a, p);
			continue;
		}
		/* insert split page */
		sppage *o = NULL;
		int rc = sp_catset(&s->s, p, &o);
		if (spunlikely(rc == -1)) {
			sp_unlock(&s->locks);
			return sp_e(s, SPEOOM, "failed to allocate page index page");
		}
		assert(o == NULL);
		sp_pageattach(p);
		m->x->n++;
	}
	sp_unlock(&s->locks);
	return 0;
}

static inline int sp_mergeN(sp *s, spepoch *x, spi *index)
{
	int rc;
	spii i;
	sp_iopen(&i, index);
	spupdate u;
	while (sp_mergeget(s, &i, &u))
	{
		spmerge m;
		sp_mergeinit(x, &m, &u, &i);
		spsplit l;
		sp_splitinit(&l);
		while (sp_active(s)) {
			rc = sp_split(s, &u, &m, &l);
			if (spunlikely(rc == 0))
				break;
			else
			if (spunlikely(rc == -1)) {
				sp_splitfree(s, &l);
				return -1;
			}
		}
		if (spunlikely(! sp_active(s)))
			return 0;
		rc = sp_splitcommit(s, &u, &m, &l);
		if (spunlikely(rc == -1)) {
			sp_splitfree(s, &l);
			return -1;
		}
		i = m.i;
	}
	return 0;
}

int sp_merge(sp *s)
{
	sp_lock(&s->lockr);
	sp_lock(&s->locki);

	spepoch *x = sp_replive(&s->rep);
	/* rotate current live epoch */
	sp_repset(&s->rep, x, SPXFER);
	int rc = sp_rotate(s);
	if (spunlikely(rc == -1)) {
		sp_lock(&s->lockr);
		sp_lock(&s->locki);
		return -1;
	}
	/* swap index */
	spi *index = sp_iswap(s);

	sp_unlock(&s->lockr);
	sp_unlock(&s->locki);

	/* complete old live epoch log */ 
	rc = sp_logeof(&x->log);
	if (spunlikely(rc == -1))
		return sp_e(s, SPEIO, "failed to write eof marker", x->epoch);
	rc = sp_logcomplete(&x->log);
	if (spunlikely(rc == -1))
		return sp_e(s, SPEIO, "failed to complete log file", x->epoch);

	/* create db file */
	rc = sp_mapepochnew(&x->db, s->e->dbnewsize, s->e->dir, x->epoch, "db");
	if (spunlikely(rc == -1))
		return sp_e(s, SPEIO, "failed to create db file", x->epoch);

	/* merge index */
	if (splikely(s->s.count > 0))
		rc = sp_mergeN(s, x, index);
	else
		rc = sp_merge0(s, x, index);

	/* check cancellation point */
	if (! sp_active(s)) {
		sp_mapunlink(&x->db);
		sp_mapclose(&x->db);
		return rc;
	}
	if (spunlikely(rc == -1))
		return -1;

	/* gc */
	if (s->e->gc) {
		rc = sp_gc(s, x);
		if (spunlikely(rc == -1))
			return -1;
	}

	/* sync/truncate db file and remap read-only only if
	 * database file is not empty. */
	if (splikely(x->db.used > 0)) {
		sp_lock(&x->lock);
		rc = sp_mapcomplete(&x->db);
		if (spunlikely(rc == -1)) {
			sp_unlock(&x->lock);
			return sp_e(s, SPEIO, "failed to complete db file", x->epoch);
		}
		sp_unlock(&x->lock);
		/* set epoch as db */
		sp_lock(&s->lockr);
		sp_repset(&s->rep, x, SPDB);
		sp_unlock(&s->lockr);
		/* remove log file */
		rc = sp_logunlink(&x->log);
		if (spunlikely(rc == -1))
			return sp_e(s, SPEIO, "failed to unlink log file", x->epoch);
		rc = sp_logclose(&x->log);
		if (spunlikely(rc == -1))
			return sp_e(s, SPEIO, "failed to close log file", x->epoch);
	} else {
		/* there are possible situation when all keys has
		 * been deleted. */
		rc = sp_mapunlink(&x->db);
		if (spunlikely(rc == -1))
			return sp_e(s, SPEIO, "failed to unlink db file", x->epoch);
		rc = sp_mapclose(&x->db);
		if (spunlikely(rc == -1))
			return sp_e(s, SPEIO, "failed to close db file", x->epoch);
	}

	/* remove all xfer epochs that took part in the merge
	 * including current, if it's database file
	 * is empty. */
	while (sp_active(s)) {
		sp_lock(&s->lockr);
		spepoch *e = sp_repxfer(&s->rep);
		sp_unlock(&s->lockr);
		if (e == NULL)
			break;
		rc = sp_logunlink(&e->log);
		if (spunlikely(rc == -1))
			return sp_e(s, SPEIO, "failed to unlink log file", e->epoch);
		rc = sp_logclose(&e->log);
		if (spunlikely(rc == -1))
			return sp_e(s, SPEIO, "failed to close log file", e->epoch);
		sp_lock(&s->lockr);
		sp_repdetach(&s->rep, e);
		sp_free(&s->a, e);
		sp_unlock(&s->lockr);
	}

	/* truncate the index (skip index on a read) */
	sp_iskipset(s, 1);
	rc = sp_itruncate(index);
	if (spunlikely(rc == -1)) {
		sp_iskipset(s, 0);
		return sp_e(s, SPE, "failed create index");
	}
	sp_iskipset(s, 0);
	return 0;
}
