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

#ifndef BEAST_THREADS_SEMAPHORE_H_INCLUDED
#define BEAST_THREADS_SEMAPHORE_H_INCLUDED

#include <condition_variable>
#include <mutex>

namespace beast {

template <class Mutex, class CondVar>
class basic_semaphore
{
private:
    typedef std::unique_lock <Mutex> scoped_lock;

    Mutex m_mutex;
    CondVar m_cond;
    std::size_t m_count;

public:
    typedef std::size_t size_type;

    /** Create the semaphore, with an optional initial count.
        If unspecified, the initial count is zero.
    */
    explicit basic_semaphore (size_type count = 0)
        : m_count (count)
    {
    }

    /** Increment the count and unblock one waiting thread. */
    void notify ()
    {
        scoped_lock lock (m_mutex);
        ++m_count;
        m_cond.notify_one ();
    }

    // Deprecated, for backward compatibility
    void signal () { notify (); }

    /** Block until notify is called. */
    void wait ()
    {
        scoped_lock lock (m_mutex);
        while (m_count == 0)
            m_cond.wait (lock);
        --m_count;
    }

    /** Perform a non-blocking wait.
        @return `true` If the wait would be satisfied.
    */
    bool try_wait ()
    {
        scoped_lock lock (m_mutex);
        if (m_count == 0)
            return false;
        --m_count;
        return true;
    }
};

typedef basic_semaphore <std::mutex, std::condition_variable> semaphore;
}

#endif

