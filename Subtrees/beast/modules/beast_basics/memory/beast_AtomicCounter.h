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

#ifndef BEAST_ATOMICCOUNTER_BEASTHEADER
#define BEAST_ATOMICCOUNTER_BEASTHEADER

/*============================================================================*/
/**
    A thread safe usage counter.

    This provides a simplified interface to an atomic integer suitable for
    measuring reference or usage counts. The counter is signaled when the
    count is non zero.

    @ingroup beast_core
*/
class AtomicCounter
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
