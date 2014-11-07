//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Portions of this file are from JUCE.
    Copyright (c) 2013 - Raw Material Software Ltd.
    Please visit http://www.juce.com

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef BEAST_THREADS_SPINLOCK_H_INCLUDED
#define BEAST_THREADS_SPINLOCK_H_INCLUDED

#include <beast/threads/UnlockGuard.h>
#include <beast/utility/noexcept.h>
#include <atomic>
#include <cassert>
#include <mutex>
#include <thread>

namespace beast {

//==============================================================================
/**
    A simple spin-lock class that can be used as a simple, low-overhead mutex for
    uncontended situations.

    Note that unlike a CriticalSection, this type of lock is not re-entrant, and may
    be less efficient when used it a highly contended situation, but it's very small and
    requires almost no initialisation.
    It's most appropriate for simple situations where you're only going to hold the
    lock for a very brief time.

    @see CriticalSection
*/
class SpinLock
{
public:
    /** Provides the type of scoped lock to use for locking a SpinLock. */
    typedef std::lock_guard <SpinLock> ScopedLockType;

    /** Provides the type of scoped unlocker to use with a SpinLock. */
    typedef UnlockGuard <SpinLock>     ScopedUnlockType;

    SpinLock()
        : m_lock (0)
    {
    }

    ~SpinLock() = default;

    SpinLock (SpinLock const&) = delete;
    SpinLock& operator= (SpinLock const&) = delete;

    /** Attempts to acquire the lock, returning true if this was successful. */
    inline bool tryEnter() const noexcept
    {
        return (m_lock.exchange (1) == 0);
    }

    /** Acquires the lock.
        This will block until the lock has been successfully acquired by this thread.
        Note that a SpinLock is NOT re-entrant, and is not smart enough to know whether the
        caller thread already has the lock - so if a thread tries to acquire a lock that it
        already holds, this method will never return!

        It's strongly recommended that you never call this method directly - instead use the
        ScopedLockType class to manage the locking using an RAII pattern instead.
    */
    void enter() const noexcept
    {
        if (! tryEnter())
        {
            for (int i = 20; --i >= 0;)
            {
                if (tryEnter())
                    return;
            }

            while (! tryEnter())
                std::this_thread::yield ();
        }
    }

    /** Releases the lock. */
    inline void exit() const noexcept
    {
        // Agh! Releasing a lock that isn't currently held!
        assert (m_lock.load () == 1);
        m_lock.store (0);
    }

    void lock () const
        { enter(); }
    void unlock () const
        { exit(); }
    bool try_lock () const
        { return tryEnter(); }

private:
    mutable std::atomic<int> m_lock;
};

}

#endif

