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

#ifndef BEAST_ATOMICSTATE_H_INCLUDED
#define BEAST_ATOMICSTATE_H_INCLUDED

/*============================================================================*/
/**
    A thread safe state variable.

    This provides a simplified interface to an integer used to control atomic
    state transitions. A state is distinguished by a single integer value.

    @ingroup beast_core
*/
class BEAST_API AtomicState
{
public:
    /** Create a new state with an optional starting value.

        @param initialState The initial state.
    */


    explicit AtomicState (const int initialState = 0) noexcept
:
    m_value (initialState)
    {
    }

    /** Retrieve the current state.

        This converts the object to an integer reflecting the current state.

        Note that other threads may change the value immediately after this
        function returns. The caller is responsible for synchronizing.

        @return The state at the time of the call.
    */
    inline operator int () const
    {
        return m_value.get ();
    }

    /** Attempt a state transition.

        The current state is compared to `from`, and if the comparison is
        successful the state becomes `to`. The entire operation is atomic.

        @param from   The current state, for comparison.

        @param to     The desired new state.

        @return       true if the state transition succeeded.
    */
    inline bool tryChangeState (const int from, const int to) noexcept
    {
        return m_value.compareAndSetBool (to, from);
    }

    /** Perform a state transition.

        This attempts to change the state and generates a diagnostic on
        failure. This routine can be used instead of tryChangeState()
        when program logic requires that the state change must succeed.

        @param from   The required current state.

        @param to     The new state.
    */
    inline void changeState (const int from, const int to) noexcept
    {
#if BEAST_DEBUG
        const bool success = tryChangeState (from, to);
        bassert (success);
#else
        tryChangeState (from, to);
#endif
    }

private:
    Atomic <int> m_value;
};

#endif
