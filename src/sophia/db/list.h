#ifndef SP_LIST_H_
#define SP_LIST_H_

/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

typedef struct splist splist;

struct splist {
	splist *next, *prev;
};

static inline void
sp_listinit(splist *h) {
	h->next = h->prev = h;
}

static inline void
sp_listappend(splist *h, splist *n) {
	n->next = h;
	n->prev = h->prev;
	n->prev->next = n;
	n->next->prev = n;
}

static inline void
sp_listunlink(splist *n) {
	n->prev->next = n->next;
	n->next->prev = n->prev;
}

static inline void
sp_listpush(splist *h, splist *n) {
	n->next = h->next;
	n->prev = h;
	n->prev->next = n;
	n->next->prev = n;
}

static inline splist*
sp_listpop(splist *h) {
	register splist *pop = h->next;
	sp_listunlink(pop);
	return pop;
}

static inline int
sp_listempty(splist *l) {
	return l->next == l && l->prev == l;
}

static inline void
sp_listmerge(splist *a, splist *b) {
	if (spunlikely(sp_listempty(b)))
		return;
	register splist *first = b->next;
	register splist *last = b->prev;
	first->prev = a->prev;
	a->prev->next = first;
	last->next = a;
	a->prev = last;
}

static inline void
sp_listreplace(splist *o, splist *n) {
	n->next = o->next;
	n->next->prev = n;
	n->prev = o->prev;
	n->prev->next = n;
}

#define sp_listlast(H, N) ((H) == (N))

#define sp_listforeach(H, I) \
	for (I = (H)->next; I != H; I = (I)->next)

#define sp_listforeach_continue(H, I) \
	for (; I != H; I = (I)->next)

#define sp_listforeach_safe(H, I, N) \
	for (I = (H)->next; I != H && (N = I->next); I = N)

#define sp_listforeach_reverse(H, I) \
	for (I = (H)->prev; I != H; I = (I)->prev)

#endif
