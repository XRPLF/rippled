
/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

#include <sp.h>

void sp_repinit(sprep *r, spa *a) {
	sp_listinit(&r->l);
	r->a = a;
	r->n = 0;
	r->ndb = 0;
	r->nxfer = 0;
	r->epoch = 0;
}

void sp_repfree(sprep *r) {
	splist *i, *n;
	sp_listforeach_safe(&r->l, i, n) {
		spepoch *e = spcast(i, spepoch, link);
		sp_lockfree(&e->lock);
		sp_free(r->a, e);
	}
}

static inline int sp_repcmp(const void *p1, const void *p2) {
	register const spepoch *a = *(spepoch**)p1;
	register const spepoch *b = *(spepoch**)p2;
	assert(a->epoch != b->epoch);
	return (a->epoch > b->epoch)? 1: -1;
}

int sp_repprepare(sprep *r) {
	spepoch **a = sp_malloc(r->a, sizeof(spepoch*) * r->n);
	if (spunlikely(a == NULL))
		return -1;
	uint32_t epoch = 0;
	int j = 0;
	splist *i;
	sp_listforeach(&r->l, i) {
		a[j] = spcast(i, spepoch, link);
		if (a[j]->epoch > epoch)
			epoch = a[j]->epoch;
		j++;
	}
	qsort(a, r->n, sizeof(spepoch*), sp_repcmp);
	sp_listinit(&r->l);
	j = 0;
	while (j < r->n) {
		sp_listinit(&a[j]->link);
		sp_listappend(&r->l, &a[j]->link);
		j++;
	}
	sp_free(r->a, a);
	r->epoch = epoch;
	return 0;
}

spepoch *sp_repmatch(sprep *r, uint32_t epoch) {
	splist *i;
	sp_listforeach(&r->l, i) {
		spepoch *e = spcast(i, spepoch, link);
		if (e->epoch == epoch)
			return e;
	}
	return NULL;
}

spepoch *sp_repalloc(sprep *r, uint32_t epoch)
{
	spepoch *e = sp_malloc(r->a, sizeof(spepoch));
	if (spunlikely(e == NULL))
		return NULL;
	memset(e, 0, sizeof(spepoch));
	e->recover = SPRNONE;
	e->epoch = epoch;
	e->type = SPUNDEF;
	e->nupdate = 0;
	e->n = 0;
	e->ngc = 0;
	sp_lockinit(&e->lock);
	sp_fileinit(&e->db, r->a);
	sp_fileinit(&e->log, r->a);
	sp_listinit(&e->pages);
	sp_listinit(&e->link);
	return e;
}

void sp_repattach(sprep *r, spepoch *e) {
	sp_listappend(&r->l, &e->link);
	r->n++;
}

void sp_repdetach(sprep *r, spepoch *e) {
	sp_listunlink(&e->link);
	r->n--;
	sp_repset(r, e, SPUNDEF);
}

void sp_repset(sprep *r, spepoch *e, spepochtype t) {
	switch (t) {
	case SPUNDEF:
		if (e->type == SPXFER)
			r->nxfer--;
		else
		if (e->type == SPDB)
			r->ndb--;
		break;
	case SPLIVE:	
		assert(e->type == SPUNDEF);
		break;
	case SPXFER:	
		assert(e->type == SPLIVE || e->type == SPUNDEF);
		r->nxfer++;
		break;
	case SPDB:	
		assert(e->type == SPXFER || e->type == SPUNDEF);
		if (e->type == SPXFER)
			r->nxfer--;
		r->ndb++;
		break;
	}
	e->type = t;
}
