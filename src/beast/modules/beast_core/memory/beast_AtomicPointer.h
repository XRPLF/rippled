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

#ifndef BEAST_ATOMICPOINTER_H_INCLUDED
#define BEAST_ATOMICPOINTER_H_INCLUDED

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
