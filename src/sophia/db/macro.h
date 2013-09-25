#ifndef SP_MACRO_H_
#define SP_MACRO_H_

/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

#define sppacked __attribute__((packed))
#define spunused __attribute__((unused))
#define sphot __attribute__((hot))
#define splikely(EXPR) __builtin_expect(!! (EXPR), 1)
#define spunlikely(EXPR) __builtin_expect(!! (EXPR), 0)
#define spdiv(a, b) ((a) + (b) - 1) / (b)
#define spcast(N, T, F) ((T*)((char*)(N) - __builtin_offsetof(T, F)))

#endif
