#pragma once

#include <shared_mutex>

// On Linux (glibc), std::shared_mutex wraps pthread_rwlock_t initialised
// with PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP.  This means a
// pending exclusive lock() blocks new shared (reader) acquisitions,
// causing reader starvation when writers contend frequently.
//
// On macOS / ARM (libc++), std::shared_mutex is already reader-preferring,
// so the same code behaves differently across platforms.
//
// This header provides reader_preferring_shared_mutex:
//   - On Linux it wraps pthread_rwlock_t initialised with
//     PTHREAD_RWLOCK_PREFER_READER_NP, matching macOS semantics.
//   - On all other platforms it is a type alias for std::shared_mutex.
//
// The interface is identical to std::shared_mutex, so it works with
// std::shared_lock and std::unique_lock.

#if defined(__linux__)

#include <cerrno>
#include <pthread.h>
#include <stdexcept>

namespace xrpl {

class reader_preferring_shared_mutex
{
    pthread_rwlock_t rwlock_;

public:
    reader_preferring_shared_mutex()
    {
        pthread_rwlockattr_t attr;
        pthread_rwlockattr_init(&attr);
        pthread_rwlockattr_setkind_np(&attr, PTHREAD_RWLOCK_PREFER_READER_NP);
        int rc = pthread_rwlock_init(&rwlock_, &attr);
        pthread_rwlockattr_destroy(&attr);
        if (rc != 0)
            throw std::system_error(
                rc, std::system_category(), "pthread_rwlock_init");
    }

    ~reader_preferring_shared_mutex()
    {
        pthread_rwlock_destroy(&rwlock_);
    }

    reader_preferring_shared_mutex(
        reader_preferring_shared_mutex const&) = delete;
    reader_preferring_shared_mutex&
    operator=(reader_preferring_shared_mutex const&) = delete;

    // Exclusive (writer) locking
    void
    lock()
    {
        pthread_rwlock_wrlock(&rwlock_);
    }

    bool
    try_lock()
    {
        return pthread_rwlock_trywrlock(&rwlock_) == 0;
    }

    void
    unlock()
    {
        pthread_rwlock_unlock(&rwlock_);
    }

    // Shared (reader) locking
    void
    lock_shared()
    {
        pthread_rwlock_rdlock(&rwlock_);
    }

    bool
    try_lock_shared()
    {
        return pthread_rwlock_tryrdlock(&rwlock_) == 0;
    }

    void
    unlock_shared()
    {
        pthread_rwlock_unlock(&rwlock_);
    }
};

}  // namespace xrpl

#else  // !__linux__

namespace xrpl {

// macOS, Windows, etc. — std::shared_mutex is already reader-preferring.
using reader_preferring_shared_mutex = std::shared_mutex;

}  // namespace xrpl

#endif
