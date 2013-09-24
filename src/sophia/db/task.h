#ifndef SP_TASK_H_
#define SP_TASK_H_

/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

typedef struct sptask sptask;

struct sptask {
	volatile int run;
	void *arg;
	pthread_t id;
	pthread_mutex_t l;
	pthread_cond_t c;
};

static inline int
sp_taskstart(sptask *t, void*(*f)(void*), void *arg) {
	t->run = 1;
	t->arg = arg;
	pthread_mutex_init(&t->l, NULL);
	pthread_cond_init(&t->c, NULL);
	return pthread_create(&t->id, NULL, f, t);
}

static inline int
sp_taskstop(sptask *t) {
	pthread_mutex_lock(&t->l);
	if (t->run == 0) {
		pthread_mutex_unlock(&t->l);
		return 0;
	}
	t->run = 0;
	pthread_cond_signal(&t->c);
	pthread_mutex_unlock(&t->l);
	return pthread_join(t->id, NULL);
}

static inline void
sp_taskwakeup(sptask *t) {
	pthread_mutex_lock(&t->l);
	pthread_cond_signal(&t->c);
	pthread_mutex_unlock(&t->l);
}

static inline int
sp_taskwait(sptask *t) {
	pthread_mutex_lock(&t->l);
	if (t->run == 0) {
		pthread_mutex_unlock(&t->l);
		return 0;
	}
	pthread_cond_wait(&t->c, &t->l);
	pthread_mutex_unlock(&t->l);
	return t->run;
}

static inline void
sp_taskdone(sptask *t) {
	pthread_mutex_lock(&t->l);
	t->run = 0;
	pthread_mutex_unlock(&t->l);
}

#endif
