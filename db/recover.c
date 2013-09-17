
/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

#include <sp.h>
#include <track.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

static inline int sp_dircreate(sp *s) {
	int rc = mkdir(s->e->dir, 0700);
	if (spunlikely(rc == -1)) {
		sp_e(s, SPE, "failed to create directory %s (errno: %d, %s)",
		     s->e->dir, errno, strerror(errno));
		return -1;
	}
	return 0;
}

static inline ssize_t
sp_epochof(char *s) {
	size_t v = 0;
	while (*s && *s != '.') {
		if (spunlikely(!isdigit(*s)))
			return -1;
		v = (v * 10) + *s - '0';
		s++;
	}
	return v;
}

static int sp_diropen(sp *s)
{
	/* read repository and determine states */
	DIR *d = opendir(s->e->dir);
	if (spunlikely(d == NULL)) {
		sp_e(s, SPE, "failed to open directory %s (errno: %d, %s)",
		     s->e->dir, errno, strerror(errno));
		return -1;
	}
	struct dirent *de;
	while ((de = readdir(d))) {
		if (*de->d_name == '.')
			continue;
		ssize_t epoch = sp_epochof(de->d_name);
		if (epoch == -1)
			continue;
		spepoch *e = sp_repmatch(&s->rep, epoch);
		if (e == NULL) {
			e = sp_repalloc(&s->rep, epoch);
			if (spunlikely(e == NULL)) {
				closedir(d);
				sp_e(s, SPEOOM, "failed to allocate repository");
				return -1;
			}
			sp_repattach(&s->rep, e);
		}
		char *ext = strstr(de->d_name, ".db");
		if (ext) {
			ext = strstr(de->d_name, ".incomplete");
			e->recover |= (ext? SPRDBI: SPRDB);
			continue;
		}
		ext = strstr(de->d_name, ".log");
		if (ext) {
			ext = strstr(de->d_name, ".incomplete");
			e->recover |= (ext? SPRLOGI: SPRLOG);
		}
		continue;
	}
	closedir(d);
	if (s->rep.n == 0)
		return 0;
	/* set current and sort by epoch */
	int rc = sp_repprepare(&s->rep);
	if (spunlikely(rc == -1))
		return sp_e(s, SPEOOM, "failed to allocate repository");
	return 0;
}

static int sp_recoverdb(sp *s, spepoch *x, sptrack *t)
{
	int rc = sp_mapepoch(&x->db, s->e->dir, x->epoch, "db");
	if (spunlikely(rc == -1))
		return sp_e(s, SPEIO, "failed to open db file", x->epoch);

	sppageh *h = (sppageh*)(x->db.map);

	for(;;)
	{
		if (spunlikely((uint64_t)((char*)h - x->db.map) >= x->db.size))
			break;

		/* validate header */
		uint32_t crc = sp_crc32c(0, &h->id, sizeof(sppageh) - sizeof(h->crc));
		if (crc != h->crc) {
			sp_mapclose(&x->db);
			return sp_e(s, SPE, "page crc failed %"PRIu32".db", x->epoch);
		}
		assert(h->id > 0);

		x->n++;
		x->nupdate += h->count;

		/* match page in hash by h.id, skip if matched */
		if (sp_trackhas(t, h->id)) {
			/* skip to a next page */
			h = (sppageh*)((char*)h + sizeof(sppageh) + h->size);
			x->ngc++;
			continue;
		}

		/* track page id */
		rc = sp_trackset(t, h->id);
		if (spunlikely(rc == -1)) {
			sp_mapclose(&x->db);
			return sp_e(s, SPEOOM, "failed to allocate track item");
		}

		/* if this is a page delete marker, then skip to
		 * a next page */
		if (h->count == 0) {
			h = (sppageh*)((char*)h + sizeof(sppageh) + h->size);
			continue;
		}

		/* set page min (first block)*/
		spvh *minp = (spvh*)((char*)h + sizeof(sppageh));
		crc = sp_crc32c(0, minp->key, minp->size);
		crc = sp_crc32c(crc, (char*)h + minp->voffset, minp->vsize);
		crc = sp_crc32c(crc, (char*)&minp->size, sizeof(spvh) - sizeof(minp->crc));
		if (crc != minp->crc) {
			sp_mapclose(&x->db);
			return sp_e(s, SPE, "page min key crc failed %"PRIu32".db", x->epoch);
		}
		assert(minp->flags == SPSET);

		/* set page max (last block) */
		spvh *maxp = (spvh*)((char*)h + sizeof(sppageh) + h->bsize * (h->count - 1));
		crc = sp_crc32c(0, maxp->key, maxp->size);
		crc = sp_crc32c(crc, (char*)h + maxp->voffset, maxp->vsize);
		crc = sp_crc32c(crc, (char*)&maxp->size, sizeof(spvh) - sizeof(maxp->crc));
		if (crc != maxp->crc) {
			sp_mapclose(&x->db);
			return sp_e(s, SPE, "page max key crc failed %"PRIu32".db", x->epoch);
		}
		assert(maxp->flags == SPSET);

		spv *min = sp_vnewh(s, minp);
		if (spunlikely(min == NULL)) {
			sp_mapclose(&x->db);
			return sp_e(s, SPEOOM, "failed to allocate key");
		}
		assert(min->flags == SPSET);
		min->epoch = x->epoch;

		spv *max = sp_vnewh(s, maxp);
		if (spunlikely(max == NULL)) {
			sp_free(&s->a, min);
			sp_mapclose(&x->db);
			return sp_e(s, SPEOOM, "failed to allocate key");
		}
		assert(max->flags == SPSET);
		max->epoch = x->epoch;

		/* allocate and insert new page */
		sppage *page = sp_pagenew(s, x);
		if (spunlikely(page == NULL)) {
			sp_free(&s->a, min);
			sp_free(&s->a, max);
			sp_mapclose(&x->db);
			return sp_e(s, SPEOOM, "failed to allocate page");
		}
		page->id = h->id;
		page->offset = (char*)h - x->db.map;
		page->size = sizeof(sppageh) + h->size;
		page->min = min;
		page->max = max;

		sppage *o = NULL;
		rc = sp_catset(&s->s, page, &o);
		if (spunlikely(rc == -1)) {
			sp_pagefree(s, page);
			sp_mapclose(&x->db);
			return sp_e(s, SPEOOM, "failed to allocate page index page");
		}
		assert(o == NULL);

		/* attach page to the source */
		sp_pageattach(page);

		/* skip to a next page */
		h = (sppageh*)((char*)h + sizeof(sppageh) + h->size);
	}

	return 0;
}

static int sp_recoverlog(sp *s, spepoch *x, int incomplete)
{
	/* open and map log file */
	char *ext = (incomplete ? "log.incomplete" : "log");
	int rc;
	rc = sp_mapepoch(&x->log, s->e->dir, x->epoch, ext);
	if (spunlikely(rc == -1))
		return sp_e(s, SPEIO, "failed to open log file", x->epoch);

	/* validate log header */
	if (spunlikely(! sp_mapinbound(&x->log, sizeof(splogh)) ))
		return sp_e(s, SPE, "bad log file %"PRIu32".log", x->epoch);

	splogh *h = (splogh*)(x->log.map);
	if (spunlikely(h->magic != SPMAGIC))
		return sp_e(s, SPE, "log bad magic %"PRIu32".log", x->epoch);
	if (spunlikely(h->version[0] != SP_VERSION_MAJOR &&
	               h->version[1] != SP_VERSION_MINOR))
		return sp_e(s, SPE, "unknown file version of %"PRIu32".log", x->epoch);

	uint64_t offset = sizeof(splogh);
	uint32_t unique = 0;
	int eof = 0;
	while (offset < x->log.size)
	{
		/* check for a eof */
		if (spunlikely(offset == (x->log.size - sizeof(speofh)))) {
			speofh *eofh = (speofh*)(x->log.map + offset);
			if (eofh->magic != SPEOF) {
				sp_mapclose(&x->log);
				return sp_e(s, SPE, "bad log eof magic %"PRIu32".log", x->epoch);
			}
			eof++;
			offset += sizeof(speofh);
			break;
		}

		/* validate a record */
		if (spunlikely(! sp_mapinbound(&x->log, offset + sizeof(spvh)) )) {
			sp_mapclose(&x->log);
			return sp_e(s, SPE, "log file corrupted %"PRIu32".log", x->epoch);
		}
		spvh *vh = (spvh*)(x->log.map + offset);

		uint32_t crc0, crc1;
		crc0 = sp_crc32c(0, vh->key, vh->size);
		crc0 = sp_crc32c(crc0, vh->key + vh->size, vh->vsize);
		crc1 = sp_crc32c(crc0, &vh->size, sizeof(spvh) - sizeof(vh->crc));
		if (spunlikely(crc1 != vh->crc)) {
			sp_mapclose(&x->log);
			return sp_e(s, SPE, "log record crc failed %"PRIu32".log", x->epoch);
		}

		int c0 = vh->flags != SPSET && vh->flags != SPDEL;
		int c1 = vh->voffset != 0;
		int c2 = !sp_mapinbound(&x->log, offset + sizeof(spvh) + vh->size +
		                        vh->vsize);

		if (spunlikely((c0 + c1 + c2) > 0)) {
			sp_mapclose(&x->log);
			return sp_e(s, SPE, "bad log record %"PRIu32".log", x->epoch);
		}

		/* add a key to the key index.
		 *
		 * key index have only actual key, replace should be done
		 * within the same epoch by a newest records only and skipped
		 * in a older epochs.
		 */
		spv *v = sp_vnewv(s, vh->key, vh->size, vh->key + vh->size, vh->vsize);
		if (spunlikely(v == NULL)) {
			sp_mapclose(&x->log);
			return sp_e(s, SPEOOM, "failed to allocate key");
		}
		v->flags = vh->flags;
		v->epoch = x->epoch;
		v->crc = crc0;

		spii pos;
		switch (sp_isetorget(s->i, v, &pos)) {
		case  1: {
			spv *old = sp_ival(&pos);
			if (old->epoch == x->epoch) {
				sp_ivalset(&pos, v);
				sp_free(&s->a, old);
			} else {
				sp_free(&s->a, v);
			}
			break;
		}
		case  0:
			unique++;
			break;
		case -1:
			sp_mapclose(&x->log);
			return sp_e(s, SPEOOM, "failed to allocate key index page");
		}

		offset += sizeof(spvh) + vh->size + vh->vsize;
		x->nupdate++;
	}

	if ((offset > x->log.size) || ((offset < x->log.size) && !eof)) {
		sp_mapclose(&x->log);
		return sp_e(s, SPE, "log file corrupted %"PRIu32".log", x->epoch);
	}

	/* unmap file only, unlink-close will ocurre in merge or
	 * during shutdown */
	rc = sp_mapunmap(&x->log);
	if (spunlikely(rc == -1))
		return sp_e(s, SPEIO, "failed to unmap log file", x->epoch);

	/*
	 * if there is eof marker missing, try to add one
	 * (only for incomplete files), otherwise indicate corrupt
	*/
	if (incomplete == 0 && !eof)
		return sp_e(s, SPE, "bad log eof marker %"PRIu32".log", x->epoch);

	if (incomplete) {
		if (! eof) {
			rc = sp_logclose(&x->log);
			if (spunlikely(rc == -1))
				return sp_e(s, SPEIO, "failed to close log file", x->epoch);
			rc = sp_logcontinue(&x->log, s->e->dir, x->epoch);
			if (spunlikely(rc == -1)) {
				sp_logclose(&x->log);
				return sp_e(s, SPEIO, "failed to reopen log file", x->epoch);
			}
			rc = sp_logeof(&x->log);
			if (spunlikely(rc == -1)) {
				sp_logclose(&x->log);
				return sp_e(s, SPEIO, "failed to add eof marker", x->epoch);
			}
		}
		rc = sp_logcompleteforce(&x->log);
		if (spunlikely(rc == -1)) {
			sp_logclose(&x->log);
			return sp_e(s, SPEIO, "failed to complete log file", x->epoch);
		}
	}
	return 0;
}

static int sp_dirrecover(sp *s)
{
	sptrack t;
	int rc = sp_trackinit(&t, &s->a, 1024);
	if (spunlikely(rc == -1))
		return sp_e(s, SPEOOM, "failed to allocate track");

	/* recover from yongest epochs (biggest numbers) */
	splist *i;
	sp_listforeach_reverse(&s->rep.l, i){
		spepoch *e = spcast(i, spepoch, link);
		switch (e->recover) {
		case SPRDB|SPRLOG:
		case SPRDB:
			sp_repset(&s->rep, e, SPDB);
			rc = sp_recoverdb(s, e, &t);
			if (spunlikely(rc == -1))
				goto err;
			if (e->recover == (SPRDB|SPRLOG)) {
				rc = sp_epochrm(s->e->dir, e->epoch, "log");
				if (spunlikely(rc == -1))
					goto err;
			}
			break;
		case SPRLOG|SPRDBI:
			rc = sp_epochrm(s->e->dir, e->epoch, "db.incomplete");
			if (spunlikely(rc == -1))
				goto err;
		case SPRLOG:
			sp_repset(&s->rep, e, SPXFER);
			rc = sp_recoverlog(s, e, 0);
			if (spunlikely(rc == -1))
				goto err;
			break;
		case SPRLOGI:
			sp_repset(&s->rep, e, SPXFER);
			rc = sp_recoverlog(s, e, 1);
			if (spunlikely(rc == -1))
				goto err;
			break;
		default:
			/* corrupted states: */
			/*   db.incomplete */
			/*   log.incomplete + db.incomplete */
			/*   log.incomplete + db */
			sp_trackfree(&t);
			return sp_e(s, SPE, "repository is corrupted");
		}
	}

	/*
	 * set maximum loaded psn as current one.
	*/
	s->psn = t.max;

	sp_trackfree(&t);
	return 0;
err:
	sp_trackfree(&t);
	return -1;
}

int sp_recover(sp *s)
{
	int exists = sp_fileexists(s->e->dir);
	int rc;
	if (!exists) {
		if (! (s->e->flags & SPO_CREAT))
			return sp_e(s, SPE, "directory doesn't exists and no SPO_CREAT specified");
		if (s->e->flags & SPO_RDONLY)
			return sp_e(s, SPE, "directory doesn't exists");
		rc = sp_dircreate(s);
	} else {
		rc = sp_diropen(s);
		if (spunlikely(rc == -1))
			return -1;
		if (s->rep.n == 0)
			return 0;
		rc = sp_dirrecover(s);
	}
	return rc;
}
