#ifndef SP_CURSOR_H_
#define SP_CURSOR_H_

/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

typedef struct spc spc;

enum spcsrc {
	SPCNONE,
	SPCI0,
	SPCI1,
	SPCP
};

#define SPCVDUP 1
#define SPCPDUP 2

typedef enum spcsrc spcsrc;

struct spc {
	spmagic m;
	sporder o;
	sp *s;
	spii i0, i1;
	int dup;     /* last iteration duplicate flags */
	sppageh *ph;
	sppage *p;
	int pi;      /* page space index */
	spvh *pv;
	int pvi;     /* version page index */
	spcsrc vsrc; /* last iteration source */
	spref r;     /* last iteration result */
};

void sp_cursoropen(spc*, sp*, sporder, char*, int);
void sp_cursorclose(spc*);

int sp_iterate(spc*);
int sp_match(sp*, void*, size_t, void**, size_t*);

#endif
