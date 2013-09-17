
/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

#include <sp.h>

void sp_ve(spe *e, int type, va_list args)
{
	sp_lock(&e->lock);
	if (e->type != SPENONE) {
		sp_unlock(&e->lock);
		return;
	}
	e->type = type;
	switch (type) {
	case SPE: {
		char *fmt = va_arg(args, char*);
		int len = snprintf(e->e, sizeof(e->e), "error: ");
		vsnprintf(e->e + len, sizeof(e->e) - len, fmt, args);
		break;
	}
	case SPEOOM: {
		char *msg = va_arg(args, char*);
		snprintf(e->e, sizeof(e->e), "out-of-memory error: %s", msg);
		break;
	}
	case SPESYS: {
		e->errno_ = errno;
		char *msg = va_arg(args, char*);
		snprintf(e->e, sizeof(e->e), "system error: %s (errno: %d, %s)",
		         msg, e->errno_, strerror(e->errno_));
		break;
	}
	case SPEIO: {
		e->errno_ = errno;
		char *msg = va_arg(args, char*);
		uint32_t epoch = va_arg(args, uint32_t);
		snprintf(e->e, sizeof(e->e), "io error: [epoch %"PRIu32"] %s (errno: %d, %s)",
		         epoch, msg, e->errno_, strerror(e->errno_));
		break;
	}
	}
	sp_unlock(&e->lock);
}
