#ifndef SP_E_H_
#define SP_E_H_

/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

typedef struct spe spe;

enum {
	SPENONE,
	SPE,     /* general error (with format) */
	SPEOOM,  /* out of memory */
	SPESYS,  /* system + errno */
	SPEIO    /* system-io + errno */
};

struct spe {
	spspinlock lock;
	int type;
	int errno_;
	char e[256];
};

static inline void sp_einit(spe *e) {
	e->lock = 0;
	e->type = SPENONE;
	e->e[0] = 0;
	sp_lockinit(&e->lock);
}

static inline void sp_efree(spe *e) {
	sp_lockfree(&e->lock);
}

static inline int sp_eis(spe *e) {
	sp_lock(&e->lock);
	register int is = e->type != SPENONE;
	sp_unlock(&e->lock);
	return is;
}

void sp_ve(spe *e, int type, va_list args);

#endif
