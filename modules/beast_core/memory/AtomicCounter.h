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

#ifndef BEAST_ATOMICCOUNTER_H_INCLUDED
#define BEAST_ATOMICCOUNTER_H_INCLUDED

/*============================================================================*/
/**
    A thread safe usage counter.

    This provides a simplified interface to an atomic integer suitable for
    measuring reference or usage counts. The counter is signaled when the
    count is non zero.

    @ingroup beast_core
*/
class BEAST_API AtomicCounter
{
public:
    /** Create a new counter.

        @param initialValue An optional starting usage count (default is 0).
    */
    AtomicCounter (int initialValue = 0) noexcept
:
    m_value (initialValue)
    {
    }

    /** Increment the usage count.

        @return `true` if the counter became signaled.
    */
    inline bool addref () noexcept
    {
        return (++m_value) == 1;
    }

    /** Decrements the usage count.

        @return `true` if the counter became non-signaled.
    */
    inline bool release () noexcept
    {
        // Unfortunately, AllocatorWithoutTLS breaks this assert
        //bassert (isSignaled ());

        return (--m_value) == 0;
    }

    /** Determine if the counter is signaled.

        Note that another thread can cause the counter to become reset after
        this function returns true.

        @return `true` if the counter was signaled.
    */
    inline bool isSignaled () const noexcept
    {
        return m_value.get () > 0;
    }

private:
    Atomic <int> m_value;
};

#endif
