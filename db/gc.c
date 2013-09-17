
/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

#include <sp.h>

int sp_gc(sp *s, spepoch *x)
{
	/*
	 * copy all yet active pages from a epoch's
	 * databases picked for the garbage
	 * collecting.
	*/
	for (;;)
	{
		sp_lock(&s->lockr);
		spepoch *g = sp_repgc(&s->rep, s->e->gcfactor);
		sp_unlock(&s->lockr);
		if (g == NULL)
			break;

		int rc;
		splist *i, *n;
		sp_listforeach_safe(&g->pages, i, n) {
			sppage *p = spcast(i, sppage, link);

			/* map origin page and copy to db file */
			sppageh *h = (sppageh*)(g->db.map + p->offset);
			sp_lock(&x->lock);
			rc = sp_mapensure(&x->db, sizeof(sppageh) + h->size, s->e->dbgrow);
			if (spunlikely(rc == -1)) {
				sp_unlock(&x->lock);
				return sp_e(s, SPEIO, "failed to remap db file", x->epoch);
			}
			sp_unlock(&x->lock);
			memcpy(x->db.map + x->db.used, h, sizeof(sppageh) + h->size);

			/* update page location */
			sp_lock(&s->locks);
			sp_listunlink(&p->link);
			sp_listappend(&x->pages, &p->link);
			p->epoch = x;
			p->offset = x->db.used;
			sp_unlock(&s->locks);

			/* advance file pointer */
			sp_mapuse(&x->db, sizeof(sppageh) + h->size);
		}

		/*
		 * remove old files and unlink the epoch
		 * from the repository.
		*/
		rc = sp_mapunlink(&g->db);
		if (spunlikely(rc == -1))
			return sp_e(s, SPEIO, "failed to unlink db file", g->epoch);
		rc = sp_mapclose(&g->db);
		if (spunlikely(rc == -1))
			return sp_e(s, SPEIO, "failed to close db file", g->epoch);
		sp_lock(&s->lockr);
		sp_repdetach(&s->rep, g);
		sp_free(&s->a, g);
		sp_unlock(&s->lockr);
	}
	return 0;
}
