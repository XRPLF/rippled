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
// This header provides ReaderPreferringSharedMutex:
//   - On Linux it wraps pthread_rwlock_t initialised with
//     PTHREAD_RWLOCK_PREFER_READER_NP, matching macOS semantics.
//   - On all other platforms it is a type alias for std::shared_mutex.
//
// The interface is identical to std::shared_mutex, so it works with
// std::shared_lock and std::unique_lock.

#ifdef __linux__

#include <pthread.h>

#include <cerrno>
#include <stdexcept>
#include <system_error>

namespace xrpl {

class ReaderPreferringSharedMutex
{
    pthread_rwlock_t rwlock_;

public:
    ReaderPreferringSharedMutex()
    {
        pthread_rwlockattr_t attr;
        pthread_rwlockattr_init(&attr);
        pthread_rwlockattr_setkind_np(&attr, PTHREAD_RWLOCK_PREFER_READER_NP);
        int rc = pthread_rwlock_init(&rwlock_, &attr);
        pthread_rwlockattr_destroy(&attr);
        if (rc != 0)
            throw std::system_error(rc, std::system_category(), "pthread_rwlock_init");
    }

    ~ReaderPreferringSharedMutex()
    {
        pthread_rwlock_destroy(&rwlock_);
    }

    ReaderPreferringSharedMutex(ReaderPreferringSharedMutex const&) = delete;
    ReaderPreferringSharedMutex&
    operator=(ReaderPreferringSharedMutex const&) = delete;

    // Exclusive (writer) locking
    void
    lock()
    {
        int rc = pthread_rwlock_wrlock(&rwlock_);
        if (rc != 0)
            throw std::system_error(rc, std::system_category(), "pthread_rwlock_wrlock");
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
        int rc = pthread_rwlock_rdlock(&rwlock_);
        if (rc != 0)
            throw std::system_error(rc, std::system_category(), "pthread_rwlock_rdlock");
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
using ReaderPreferringSharedMutex = std::shared_mutex;

}  // namespace xrpl

#endif
