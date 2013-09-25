#ifndef SP_LOCK_H_
#define SP_LOCK_H_

/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

#include <unistd.h>

typedef uint8_t spspinlock;

#if defined(__x86_64__) || defined(__i386) || defined(_X86_)
# define CPU_PAUSE __asm__ ("pause")
#else
# define CPU_PAUSE do { } while(0)
#endif

static inline void
sp_lockinit(volatile spspinlock *l) {
	*l = 0;
}

static inline void
sp_lockfree(volatile spspinlock *l) {
	*l = 0;
}

static inline void
sp_lock(volatile spspinlock *l) {
	if (__sync_lock_test_and_set(l, 1) != 0) {
		unsigned int spin_count = 0U;
		for (;;) {
			CPU_PAUSE;
			if (*l == 0U && __sync_lock_test_and_set(l, 1) == 0)
				break;
			if (++spin_count > 100U)
				usleep(0);
		}
	}
}

static inline void
sp_unlock(volatile spspinlock *l) {
	__sync_lock_release(l);
}

#if 0
#include <pthread.h>

typedef pthread_spinlock_t spspinlock;

static inline void
sp_lockinit(volatile spspinlock *l) {
	pthread_spin_init(l, 0);
}

static inline void
sp_lockfree(volatile spspinlock *l) {
	pthread_spin_destroy(l);
}

static inline void
sp_lock(volatile spspinlock *l) {
	pthread_spin_lock(l);
}

static inline void
sp_unlock(volatile spspinlock *l) {
	pthread_spin_unlock(l);
}
#endif

#endif
