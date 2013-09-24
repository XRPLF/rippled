#ifndef SP_CAT_H_
#define SP_CAT_H_

/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

typedef struct spcat spcat;

struct spcat {
	spa *a;
	sppage **i;
	uint32_t count;
	uint32_t top;
	spcmpf cmp;
	void *cmparg;
};

int sp_catinit(spcat*, spa*, int, spcmpf, void*);
void sp_catfree(spcat*);
int sp_catset(spcat*, sppage*, sppage**);
int sp_catget(spcat*, uint64_t);
int sp_catdel(spcat*, uint32_t);
sppage *sp_catfind(spcat*, char*, int, uint32_t*);
sppage *sp_catroute(spcat*, char*, int, uint32_t*);
int sp_catown(spcat*, uint32_t, spv*);

#endif
