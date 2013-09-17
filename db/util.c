
/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

#include <sp.h>

char *sp_memdup(sp *s, void *src, size_t size)
{
	char *v = sp_malloc(&s->a, size);
	if (spunlikely(v == NULL))
		return NULL;
	memcpy(v, src, size);
	return v;
}

sppage *sp_pagenew(sp *s, spepoch *e) {
	sppage *page = sp_malloc(&s->a, sizeof(sppage));
	if (spunlikely(page == NULL))
		return NULL;
	memset(page, 0, sizeof(sppage));
	page->epoch = e;
	sp_listinit(&page->link);
	return page;
}

void sp_pageattach(sppage *p) {
	assert(p->epoch != NULL);
	sp_listappend(&((spepoch*)p->epoch)->pages, &p->link);
}

void sp_pagedetach(sppage *p) {
	assert(p->epoch != NULL);
	sp_listunlink(&p->link);
}

void sp_pagefree(sp *s, sppage *p) {
	sp_listunlink(&p->link);
	sp_free(&s->a, p->min);
	sp_free(&s->a, p->max);
	sp_free(&s->a, p);
}

static inline spv*
sp_vnewof(sp *s, void *k, uint16_t size, int reserve) {
	spv *v = sp_malloc(&s->a, sizeof(spv) + size + reserve);
	if (spunlikely(v == NULL))
		return NULL;
	v->epoch = 0;
	v->size = size;
	v->flags = 0;
	memcpy(v->key, k, size);
	return v;
}

spv *sp_vnew(sp *s, void *k, uint16_t size) {
	return sp_vnewof(s, k, size, 0);
}

spv *sp_vnewv(sp *s, void *k, uint16_t size, void *v, uint32_t vsize)
{
	spv *vn = sp_vnewof(s, k, size, sizeof(uint32_t) + vsize);
	if (spunlikely(vn == NULL))
		return NULL;
	memcpy(vn->key + vn->size, &vsize, sizeof(uint32_t));
	memcpy(vn->key + vn->size + sizeof(uint32_t), v, vsize);
	return vn;
}

spv *sp_vnewh(sp *s, spvh *v) {
	spv *vn = sp_vnewof(s, v->key, v->size, 0);
	if (spunlikely(vn == NULL))
		return NULL;
	vn->flags = v->flags;
	return vn;
}

spv *sp_vdup(sp *s, spv *v)
{
	spv *vn = sp_malloc(&s->a, sizeof(spv) + v->size);
	if (spunlikely(vn == NULL))
		return NULL;
	memcpy(vn, v, sizeof(spv) + v->size);
	return vn;
}

spv *sp_vdupref(sp *s, spref *r, uint32_t epoch)
{
	spv *vn = NULL;
	switch (r->type) {
	case SPREFM: vn = sp_vdup(s, r->v.v);
		break;
	case SPREFD: vn = sp_vnewh(s, r->v.vh);
		break;
	}
	if (spunlikely(vn == NULL))
		return NULL;
	vn->epoch = epoch;
	vn->flags = 0;
	return vn;
}
