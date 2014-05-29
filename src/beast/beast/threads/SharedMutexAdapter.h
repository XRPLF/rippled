//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_THREADS_SHAREDMUTEXADAPTER_H_INCLUDED
#define BEAST_THREADS_SHAREDMUTEXADAPTER_H_INCLUDED

#include <beast/threads/SharedLockGuard.h>

#include <mutex>

namespace beast {
   
/** Adapts a regular Lockable to conform to the SharedMutex concept.
    Shared locks become unique locks with this interface. Two threads may not
    simultaneously acquire ownership of the lock. Typically the Mutex template
    parameter will be a CriticalSection.
*/
template <class Mutex>
class SharedMutexAdapter
{
public:
    typedef Mutex MutexType;
    typedef std::lock_guard <SharedMutexAdapter> LockGuardType;
    typedef SharedLockGuard <SharedMutexAdapter> SharedLockGuardType;
    
    void lock() const
    {
        m_mutex.lock();
    }

    void unlock() const
    {
        m_mutex.unlock();
    }

    void lock_shared() const
    {
        m_mutex.lock();
    }

    void unlock_shared() const
    {
        m_mutex.unlock();
    }

private:
    Mutex mutable m_mutex;
};

}

#endif
