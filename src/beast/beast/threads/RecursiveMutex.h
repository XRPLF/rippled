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

#ifndef BEAST_THREADS_RECURSIVEMUTEX_H_INCLUDED
#define BEAST_THREADS_RECURSIVEMUTEX_H_INCLUDED

#include <beast/Config.h>
#include <beast/threads/UnlockGuard.h>
#include <beast/threads/TryLockGuard.h>

#include <mutex>

#if ! BEAST_WINDOWS
#include <pthread.h>
#endif

namespace beast {

class RecursiveMutex
{
public:
    typedef std::lock_guard <RecursiveMutex>      ScopedLockType;
    typedef UnlockGuard <RecursiveMutex>    ScopedUnlockType;
    typedef TryLockGuard <RecursiveMutex>   ScopedTryLockType;

    /** Create the mutex.
        The mutux is initially unowned.
    */
    RecursiveMutex ();

    /** Destroy the mutex.
        If the lock is owned, the result is undefined.
    */
    ~RecursiveMutex ();

    // Boost concept compatibility:
    // http://www.boost.org/doc/libs/1_54_0/doc/html/thread/synchronization.html#thread.synchronization.mutex_concepts

    /** BasicLockable */
    /** @{ */
    void lock () const;
    void unlock () const;
    /** @} */

    /** Lockable */
    bool try_lock () const;

private:
// To avoid including windows.h in the public Beast headers, we'll just
// reserve storage here that's big enough to be used internally as a windows
// CRITICAL_SECTION structure.
#if BEAST_WINDOWS
# if BEAST_64BIT
    char section[44];
# else
    char section[24];
# endif
#else
    mutable pthread_mutex_t mutex;
#endif
};

}

#endif
