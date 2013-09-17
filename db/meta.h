#ifndef SP_META_H_
#define SP_META_H_

/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

/* on-disk */

typedef struct splogh splogh;
typedef struct speofh speofh;
typedef struct sppageh sppageh;
typedef struct spvh spvh;

#define SPEOF   0x00aaeefdL
#define SPMAGIC 0x00f0e0d0L

struct splogh {
	uint32_t magic;
	uint8_t version[2];
} sppacked;

struct speofh {
	uint32_t magic;
} sppacked;

struct sppageh {
	uint32_t crc;
	uint64_t id;
	uint16_t count;
	uint32_t size;
	uint32_t bsize;
} sppacked;

struct spvh {
	uint32_t crc;
	uint32_t size;
	uint32_t voffset;
	uint32_t vsize;
	uint8_t  flags;
	char key[];
} sppacked;

/* in-memory */

typedef struct spv spv;
typedef struct sppage sppage;

#define SPSET 1
#define SPDEL 2

struct spv {
	uint32_t epoch;
	uint32_t crc; /* key-value crc without header */
	uint16_t size;
	uint8_t  flags;
	char key[];
	/* uint32_t vsize */
	/* char v[] */
} sppacked;

struct sppage {
	uint64_t id;
	uint64_t offset;
	void *epoch;
	uint32_t size;
	spv *min;
	spv *max;
	splist link;
} sppacked;

static inline char*
sp_vv(spv *v) {
	return v->key + v->size + sizeof(uint32_t);
}

static inline uint32_t
sp_vvsize(spv *v) {
	register char *p = (char*)(v->key + v->size);
	return *(uint32_t*)p;
}

#endif
