#ifndef SP_A_H_
#define SP_A_H_

/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

typedef struct spa spa;

struct spa {
	spallocf alloc;
	void *arg;
};

static inline void
sp_allocinit(spa *a, spallocf f, void *arg) {
	a->alloc = f;
	a->arg = arg;
}

static inline void*
sp_allocstd(void *ptr, size_t size, void *arg spunused) {
	if (splikely(size > 0)) {
		if (ptr != NULL)
			return realloc(ptr, size);
		return malloc(size);
	}
	assert(ptr != NULL);
	free(ptr);
	return NULL;
}

static inline void *sp_realloc(spa *a, void *ptr, size_t size) {
	return a->alloc(ptr, size, a->arg);
}

static inline void *sp_malloc(spa *a, size_t size) {
	return a->alloc(NULL, size, a->arg);
}

static inline char *sp_strdup(spa *a, char *str) {
	int sz = strlen(str) + 1;
	char *s = a->alloc(NULL, sz, a->arg);
	if (spunlikely(s == NULL))
		return NULL;
	memcpy(s, str, sz);
	return s;
}

static inline void sp_free(spa *a, void *ptr) {
	a->alloc(ptr, 0, a->arg);
}

#endif
