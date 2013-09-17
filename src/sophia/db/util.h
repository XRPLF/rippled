#ifndef SP_UTIL_H_
#define SP_UTIL_H_

/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

char *sp_memdup(sp*, void*, size_t);

spv *sp_vnew(sp*, void*, uint16_t);
spv *sp_vnewv(sp*, void*, uint16_t, void*, uint32_t);
spv *sp_vnewh(sp*, spvh*);
spv *sp_vdup(sp*, spv*);
spv *sp_vdupref(sp*, spref*, uint32_t);

sppage *sp_pagenew(sp*, spepoch*);
void sp_pagefree(sp*, sppage*);
void sp_pageattach(sppage*);
void sp_pagedetach(sppage*);

#endif
