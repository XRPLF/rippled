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

#ifndef BEAST_ATOMICFLAG_H_INCLUDED
#define BEAST_ATOMICFLAG_H_INCLUDED

/*============================================================================*/
/**
    A thread safe flag.

    This provides a simplified interface to an atomic integer suitable for
    representing a flag. The flag is signaled when on, else it is considered
    reset.

    @ingroup beast_core
*/
class BEAST_API AtomicFlag
{
public:
    /** Create an AtomicFlag in the reset state. */
    AtomicFlag () noexcept
:
    m_value (0)
    {
    }

    /** Signal the flag.

        If two or more threads simultaneously attempt to signal the flag,
        only one will receive a true return value.

        @return true if the flag was previously reset.
    */
    inline bool trySignal () noexcept
    {
        return m_value.compareAndSetBool (1, 0);
    }

    /** Signal the flag.

        The flag must be in the reset state. Only one thread may
        call this at a time.
    */
    inline void signal () noexcept
    {
#if BEAST_DEBUG
        const bool success = m_value.compareAndSetBool (1, 0);
        bassert (success);
#else
        m_value.set (1);
#endif
    }

    /** Reset the flag.

        The flag must be in the signaled state. Only one thread may
        call this at a time. Usually it is the thread that was successful
        in a previous call to trySignal().
    */
    inline void reset () noexcept
    {
#if BEAST_DEBUG
        const bool success = m_value.compareAndSetBool (0, 1);
        bassert (success);
#else
        m_value.set (0);
#endif
    }

    /** Check if the AtomicFlag is signaled

        The signaled status may change immediately after this call
        returns. The caller must synchronize.

        @return true if the flag was signaled.
    */
    inline bool isSignaled () const noexcept
    {
        return m_value.get () == 1;
    }

private:
    Atomic <int> m_value;
};

#endif
