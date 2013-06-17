/*============================================================================*/
/*
  VFLib: https://github.com/vinniefalco/VFLib

  Copyright (C) 2008 by Vinnie Falco <vinnie.falco@gmail.com>

  This library contains portions of other open source products covered by
  separate licenses. Please see the corresponding source files for specific
  terms.

  VFLib is provided under the terms of The MIT License (MIT):

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
  IN THE SOFTWARE.
*/
/*============================================================================*/

#ifndef BEAST_ATOMICSTATE_BEASTHEADER
#define BEAST_ATOMICSTATE_BEASTHEADER

/*============================================================================*/
/**
    A thread safe state variable.

    This provides a simplified interface to an integer used to control atomic
    state transitions. A state is distinguished by a single integer value.

    @ingroup beast_core
*/
class AtomicState
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
