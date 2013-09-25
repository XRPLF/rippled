#ifndef SP_TRACK_H_
#define SP_TRACK_H_

/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

typedef struct sptrack sptrack;

struct sptrack {
	spa *a;
	uint64_t max;
	uint64_t *i;
	int count;
	int size;
};

static inline int
sp_trackinit(sptrack *t, spa *a, int size) {
	t->a = a;
	t->max = 0;
	t->count = 0;
	t->size = size;
	int sz = size * sizeof(uint64_t);
	t->i = sp_malloc(a, sz);
	if (spunlikely(t->i == NULL))
		return -1;
	memset(t->i, 0, sz);
	return 0;
}

static inline void sp_trackfree(sptrack *t) {
	if (spunlikely(t->i == NULL))
		return;
	sp_free(t->a, t->i);
	t->i = NULL;
}

static inline void
sp_trackinsert(sptrack *t, uint64_t id) {
	uint32_t pos = id % t->size;
	for (;;) {
		if (spunlikely(t->i[pos] != 0)) {
			pos = (pos + 1) % t->size;
			continue;
		}
		if (id > t->max)
			t->max = id;
		t->i[pos] = id;
		break;
	}
	t->count++;
}

static inline int
sp_trackresize(sptrack *t) {
	sptrack nt;
	int rc = sp_trackinit(&nt, t->a, t->size * 2);
	if (spunlikely(rc == -1))
		return -1;
	int i = 0;
	while (i < t->size) {
		if (t->i[i])
			sp_trackinsert(&nt, t->i[i]);
		i++;
	}
	sp_trackfree(t);
	*t = nt;
	return 0;
}

static inline int
sp_trackset(sptrack *t, uint64_t id) {
	if (spunlikely(t->count > (t->size / 2)))
		if (spunlikely(sp_trackresize(t) == -1))
			return -1;
	sp_trackinsert(t, id);
	return 0;
}

static inline int
sp_trackhas(sptrack *t, uint64_t id) {
	uint32_t pos = id % t->size;
	for (;;) {
		if (spunlikely(t->i[pos] == 0))
			return 0;
		if (t->i[pos] == id)
			return 1;
		pos = (pos + 1) % t->size;
		continue;
	}
	return 0;
}

#endif
