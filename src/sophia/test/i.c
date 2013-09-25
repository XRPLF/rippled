
/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

#include <time.h>
#include <sophia.h>
#include <sp.h>
#include "test.h"

static spa a;

static inline spv *newv(uint32_t k) {
	spv *v = sp_malloc(&a, sizeof(spv) + sizeof(uint32_t));
	if (spunlikely(v == NULL))
		return NULL;
	v->epoch = 0;
	v->crc = 0;
	v->size = sizeof(k);
	v->flags = 0;
	memcpy(v->key, &k, sizeof(k));
	return v;
}

static inline void freekey(spv *v) {
	sp_free(&a, v);
}

static inline int
cmp(char *a, size_t asz, char *b, size_t bsz, void *arg) {
	register uint32_t av = *(uint32_t*)a;
	register uint32_t bv = *(uint32_t*)b;
	if (av == bv)
		return 0;
	return (av > bv) ? 1 : -1;
}

static void
init(void) {
	spi i;
	t( sp_iinit(&i, &a, 256, cmp, NULL) == 0);
	sp_ifree(&i);
}

static void
set(void) {
	spi index;
	t( sp_iinit(&index, &a, 16, cmp, NULL) == 0);
	int k = 0;
	while (k < 8) {
		spv *v = newv(k);
		spv *old = NULL;
		t( v != NULL );
		t( sp_iset(&index, v, &old) == 0);
		t( old == NULL );
		k++;
	}
	sp_ifree(&index);
}

static void
set_split(void) {
	spi index;
	t( sp_iinit(&index, &a, 16, cmp, NULL) == 0);
	int k = 0;
	while (k < 32) {
		spv *v = newv(k);
		spv *old = NULL;
		t( v != NULL );
		t( sp_iset(&index, v, &old) == 0);
		t( old == NULL );
		k++;
	}
	sp_ifree(&index);
}

static void
set_get(void) {
	spi index;
	t( sp_iinit(&index, &a, 16, cmp, NULL) == 0);
	int k = 0;
	while (k < 8) {
		spv *v = newv(k);
		spv *old = NULL;
		t( v != NULL );
		t( sp_iset(&index, v, &old) == 0);
		t( old == NULL );
		k++;
	}
	k = 0;
	while (k < 8) {
		spv *v = sp_igetraw(&index, (char*)&k, sizeof(k));
		t( v != NULL );
		t( *(uint32_t*)v->key == k );
		k++;
	}
	sp_ifree(&index);
}

static void
set_get_split(void) {
	spi index;
	t( sp_iinit(&index, &a, 16, cmp, NULL) == 0);
	int k = 0;
	while (k < 32) {
		spv *v = newv(k);
		spv *old = NULL;
		t( v != NULL );
		t( sp_iset(&index, v, &old) == 0);
		t( old == NULL );
		k++;
	}
	k = 0;
	while (k < 32) {
		spv *v = sp_igetraw(&index, (char*)&k, sizeof(k));
		t( v != NULL );
		t( *(uint32_t*)v->key == k );
		k++;
	}
	sp_ifree(&index);
}

static void
set_fetchfwd(void) {
	spi index;
	t( sp_iinit(&index, &a, 16, cmp, NULL) == 0);
	int k = 0;
	while (k < 8) {
		spv *v = newv(k);
		spv *old = NULL;
		t( v != NULL );
		t( sp_iset(&index, v, &old) == 0);
		t( old == NULL );
		k++;
	}
	spv *max = sp_imax(&index);
	t( max != NULL );
	t( *(uint32_t*)max->key == 7 );
	k = 0;
	spii it;
	sp_iopen(&it, &index);
	do {
		spv *v = sp_ival(&it);
		t( v != NULL );
		t( *(uint32_t*)v->key == k );
		k++;
	} while (sp_inext(&it));
	sp_ifree(&index);
}

static void
set_fetchbkw(void) {
	spi index;
	t( sp_iinit(&index, &a, 16, cmp, NULL) == 0);
	int k = 0;
	while (k < 8) {
		spv *v = newv(k);
		spv *old = NULL;
		t( v != NULL );
		t( sp_iset(&index, v, &old) == 0);
		t( old == NULL );
		k++;
	}
	k = 7;
	spii it;
	sp_iopen(&it, &index);
	sp_ilast(&it);
	do {
		spv *v = sp_ival(&it);
		t( v != NULL );
		t( *(uint32_t*)v->key == k );
		k--;
	} while (sp_iprev(&it));
	sp_ifree(&index);
}

static void
set_fetchfwd_split(void) {
	spi index;
	t( sp_iinit(&index, &a, 16, cmp, NULL) == 0);
	int k = 0;
	while (k < 73) {
		spv *v = newv(k);
		spv *old = NULL;
		t( v != NULL );
		t( sp_iset(&index, v, &old) == 0);
		t( old == NULL );
		k++;
	}
	k = 0;
	spii it;
	sp_iopen(&it, &index);
	do {
		spv *v = sp_ival(&it);
		t( v != NULL );
		t( *(uint32_t*)v->key == k );
		k++;
	} while (sp_inext(&it));
	sp_ifree(&index);
}

static void
set_fetchbkw_split(void) {
	spi index;
	t( sp_iinit(&index, &a, 16, cmp, NULL) == 0);
	int k = 0;
	while (k < 89) {
		spv *v = newv(k);
		spv *old = NULL;
		t( v != NULL );
		t( sp_iset(&index, v, &old) == 0);
		t( old == NULL );
		k++;
	}
	spv *max = sp_imax(&index);
	t( max != NULL );
	t( *(uint32_t*)max->key == 88 );
	k = 88;
	spii it;
	sp_iopen(&it, &index);
	sp_ilast(&it);
	do {
		spv *v = sp_ival(&it);
		t( v != NULL );
		t( *(uint32_t*)v->key == k );
		k--;
	} while (sp_iprev(&it));
	sp_ifree(&index);
}

static void
set_del(void) {
	spi index;
	t( sp_iinit(&index, &a, 16, cmp, NULL) == 0);
	int k = 0;
	while (k < 8) {
		spv *v = newv(k);
		spv *old = NULL;
		t( v != NULL );
		t( sp_iset(&index, v, &old) == 0);
		t( old == NULL );
		k++;
	}
	k = 0;
	while (k < 8) {
		spv *v = sp_igetraw(&index, (char*)&k, sizeof(k));
		t( v != NULL );
		t( *(uint32_t*)v->key == k );
		spv *old = NULL;
		t( sp_idelraw(&index, (char*)&k, sizeof(k), &old) == 1);
		t( old != NULL );
		t( *(uint32_t*)old->key == k );
		freekey(old);
		k++;
	}
	t ( index.count == 0 );
	t ( index.icount == 1 );
	spv *max = sp_imax(&index);
	t( max == NULL );
	spii it;
	sp_iopen(&it, &index);
	t( sp_ival(&it) == NULL );
	t( sp_inext(&it) == 0 );
	sp_ifree(&index);
}

static void
set_del_split(void) {
	spi index;
	t( sp_iinit(&index, &a, 16, cmp, NULL) == 0);
	int k = 0;
	while (k < 37) {
		spv *v = newv(k);
		spv *old = NULL;
		t( v != NULL );
		t( sp_iset(&index, v, &old) == 0);
		t( old == NULL );
		k++;
	}
	k = 0;
	while (k < 37) {
		spv *v = sp_igetraw(&index, (char*)&k, sizeof(k));
		t( v != NULL );
		t( *(uint32_t*)v->key == k );
		spv *old = NULL;
		t( sp_idelraw(&index, (char*)&k, sizeof(k), &old) == 1);
		t( old != NULL );
		t( *(uint32_t*)old->key == k );
		freekey(old);
		k++;
	}
	t ( index.count == 0 );
	t ( index.icount == 1 );
	spv *max = sp_imax(&index);
	t( max == NULL );
	spii it;
	sp_iopen(&it, &index);
	t( sp_ival(&it) == NULL );
	t( sp_inext(&it) == 0 );
	sp_ifree(&index);
}

static void
set_delbkw_split(void) {
	spi index;
	t( sp_iinit(&index, &a, 16, cmp, NULL) == 0);
	int k = 0;
	while (k < 37) {
		spv *v = newv(k);
		spv *old = NULL;
		t( v != NULL );
		t( sp_iset(&index, v, &old) == 0);
		t( old == NULL );
		k++;
	}
	k = 36;
	while (k >= 0) {
		spv *v = sp_igetraw(&index, (char*)&k, sizeof(k));
		t( v != NULL );
		t( *(uint32_t*)v->key == k );
		spv *old = NULL;
		t( sp_idelraw(&index, (char*)&k, sizeof(k), &old) == 1);
		t( old != NULL );
		t( *(uint32_t*)old->key == k );
		freekey(old);
		k--;
	}
	t ( index.count == 0 );
	t ( index.icount == 1 );
	spv *max = sp_imax(&index);
	t( max == NULL );
	spii it;
	sp_iopen(&it, &index);
	t( sp_ival(&it) == NULL );
	t( sp_inext(&it) == 0 );
	sp_ifree(&index);
}

static void
set_delrnd_split(void) {
	spi index;
	t( sp_iinit(&index, &a, 16, cmp, NULL) == 0);
	int count = 397;
	int k = 0;
	while (k < count) {
		spv *v = newv(k);
		spv *old = NULL;
		t( v != NULL );
		t( sp_iset(&index, v, &old) == 0);
		t( old == NULL );
		k++;
	}
	srand(time(NULL));
	int total = count;
	while (total != 0) {
		k = rand() % count;
		spv *v = sp_igetraw(&index, (char*)&k, sizeof(k));
		spv *old = NULL;
		int rc = sp_idelraw(&index, (char*)&k, sizeof(k), &old);
		if (rc == 1) {
			t( v == old );
			total--;
			t( old != NULL );
			freekey(old);
		} else {
			t( v == NULL );
		}
	}
	t ( index.count == 0 );
	t ( index.icount == 1 );
	spv *max = sp_imax(&index);
	t( max == NULL );
	spii it;
	sp_iopen(&it, &index);
	t( sp_ival(&it) == NULL );
	t( sp_inext(&it) == 0 );
	sp_ifree(&index);
}

int
main(int argc, char *argv[])
{
	sp_allocinit(&a, &sp_allocstd, NULL);

	test(init);
	test(set);
	test(set_split);
	test(set_get);
	test(set_get_split);
	test(set_fetchfwd);
	test(set_fetchbkw);
	test(set_fetchfwd_split);
	test(set_fetchbkw_split);
	test(set_del);
	test(set_del_split);
	test(set_delbkw_split);
	test(set_delrnd_split);
	return 0;
}
