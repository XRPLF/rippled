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

#ifndef BEAST_ATOMICFLAG_BEASTHEADER
#define BEAST_ATOMICFLAG_BEASTHEADER

/*============================================================================*/
/**
    A thread safe flag.

    This provides a simplified interface to an atomic integer suitable for
    representing a flag. The flag is signaled when on, else it is considered
    reset.

    @ingroup beast_core
*/
class AtomicFlag
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
