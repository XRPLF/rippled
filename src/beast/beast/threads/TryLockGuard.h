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

#ifndef BEAST_THREADS_TRYLOCKGUARD_H_INCLUDED
#define BEAST_THREADS_TRYLOCKGUARD_H_INCLUDED

namespace beast {

template <typename Mutex>
class TryLockGuard
{
public:
    typedef Mutex MutexType;

    explicit TryLockGuard (Mutex const& mutex)
        : m_mutex (mutex)
        , m_owns_lock (m_mutex.try_lock())
    {
    }

    ~TryLockGuard ()
    {
        if (m_owns_lock)
            m_mutex.unlock();
    }

    TryLockGuard (TryLockGuard const&) = delete;
    TryLockGuard& operator= (TryLockGuard const&) = delete;

    bool owns_lock() const
        { return m_owns_lock; }

private:
    Mutex const& m_mutex;
    bool m_owns_lock;
};

}

#endif
