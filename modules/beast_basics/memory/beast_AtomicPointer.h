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

#ifndef BEAST_ATOMICPOINTER_BEASTHEADER
#define BEAST_ATOMICPOINTER_BEASTHEADER

/*============================================================================*/
/**
    A thread safe pointer.

    This provides a simplified interface to an atomic pointer suitable
    for building containers or composite classes. Operator overloads
    allow access to the underlying pointer using natural C++ syntax.

    @ingroup beast_core
*/
template <class P>
class AtomicPointer
{
public:
    /** Create a pointer.

        @param initialValue An optional starting value (default is null).
    */
    explicit AtomicPointer (P* const initialValue = nullptr) noexcept
:
    m_value (initialValue)
    {
    }

    /** Retrieve the pointer value */
    inline P* get () const noexcept
    {
        return m_value.get ();
    }

    /** Obtain a pointer to P through type conversion.

        The caller must synchronize access to P.

        @return A pointer to P.
    */
    inline operator P* () const noexcept
    {
        return get ();
    }

    /** Dereference operator

        The caller must synchronize access to P.

        @return A reference to P.
    */
    inline P& operator* () const noexcept
    {
        return &get ();
    }

    /** Member selection

        The caller must synchronize access to P.

        @return A pointer to P.
    */
    inline P* operator-> () const noexcept
    {
        return get ();
    }

    inline void set (P* p)
    {
        m_value.set (p);
    }

    /** Atomically assign a new pointer

        @param newValue The new value to assign.
    */
    inline void operator= (P* newValue) noexcept
    {
        set (newValue);
    }

    /** Atomically assign a new pointer and return the old value.

        @param newValue The new value to assign.

        @return         The previous value.
    */
    inline P* exchange (P* newValue)
    {
        return m_value.exchange (newValue);
    }

    /** Conditionally perform an atomic assignment.

        The current value is compared with oldValue and atomically
        set to newValue if the comparison is equal.

        The caller is responsible for handling the ABA problem.

        @param  newValue  The new value to assign.

        @param  oldValue  The matching old value.

        @return true if the assignment was performed.
    */
    inline bool compareAndSet (P* newValue, P* oldValue)
    {
        return m_value.compareAndSetBool (newValue, oldValue);
    }

private:
    Atomic <P*> m_value;
};

#endif
