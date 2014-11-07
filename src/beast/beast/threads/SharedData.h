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

#ifndef BEAST_THREADS_SHAREDDATA_H_INCLUDED
#define BEAST_THREADS_SHAREDDATA_H_INCLUDED

#include <beast/threads/RecursiveMutex.h>
#include <beast/threads/SharedMutexAdapter.h>

namespace beast {

/** Structured, multi-threaded access to a shared state.

    This container combines locking semantics with data access semantics to
    create an alternative to the typical synchronization method of first
    acquiring a lock and then accessing data members.

    With this container, access to the underlying data is only possible after
    first acquiring a lock. The steps of acquiring the lock and obtaining
    a const or non-const reference to the data are combined into one
    RAII style operation.

    There are three types of access:

    - Access
        Provides access to the shared state via a non-const reference or pointer.
        Access acquires a unique lock on the mutex associated with the
        container.

    - ConstAccess
        Provides access to the shared state via a const reference or pointer.
        ConstAccess acquires a shared lock on the mutex associated with the
        container.

    - ConstUnlockedAccess
        Provides access to the shared state via a const reference or pointer.
        No locking is performed. It is the callers responsibility to ensure that
        the operation is synchronized. This can be useful for diagnostics or
        assertions, or for when it is known that no other threads can access
        the data.

    - UnlockedAccess
        Provides access to the shared state via a reference or pointer.
        No locking is performed. It is the callers responsibility to ensure that
        the operation is synchronized. This can be useful for diagnostics or
        assertions, or for when it is known that no other threads can access
        the data.

    Usage example:

    @code

    struct State
    {
        int value1;
        String value2;
    };

    typedef SharedData <State> SharedState;

    SharedState m_state;

    void readExample ()
    {
        SharedState::ConstAccess state (m_state);

        print (state->value1);   // read access
        print (state->value2);   // read access

        state->value1 = 42;      // write disallowed: compile error
    }

    void writeExample ()
    {
        SharedState::Access state (m_state);

        state->value2 = "Label"; // write access, allowed
    }

    @endcode

    Requirements for Value:
        Constructible
        Destructible

    Requirements for SharedMutexType:
        X is SharedMutexType, a is an instance of X:
        X a;                    DefaultConstructible
        X::LockGuardType        Names a type that implements the LockGuard concept.
        X::SharedLockGuardType  Names a type that implements the SharedLockGuard concept.

    @tparam Value The type which the container holds.
    @tparam SharedMutexType The type of shared mutex to use.
*/
template <typename Value, class SharedMutexType =
    SharedMutexAdapter <RecursiveMutex> >
class SharedData
{
private:
    typedef typename SharedMutexType::LockGuardType LockGuardType;
    typedef typename SharedMutexType::SharedLockGuardType SharedLockGuardType;

public:
    typedef Value ValueType;

    class Access;
    class ConstAccess;
    class UnlockedAccess;
    class ConstUnlockedAccess;

    /** Create a shared data container.
        Up to 8 parameters can be specified in the constructor. These parameters
        are forwarded to the corresponding constructor in Data. If no
        constructor in Data matches the parameter list, a compile error is
        generated.
    */
    /** @{ */
    SharedData () = default;

    template <class T1>
    explicit SharedData (T1 t1)
        : m_value (t1) { }

    template <class T1, class T2>
    SharedData (T1 t1, T2 t2)
        : m_value (t1, t2) { }

    template <class T1, class T2, class T3>
    SharedData (T1 t1, T2 t2, T3 t3)
        : m_value (t1, t2, t3) { }

    template <class T1, class T2, class T3, class T4>
    SharedData (T1 t1, T2 t2, T3 t3, T4 t4)
        : m_value (t1, t2, t3, t4) { }

    template <class T1, class T2, class T3, class T4, class T5>
    SharedData (T1 t1, T2 t2, T3 t3, T4 t4, T5 t5)
        : m_value (t1, t2, t3, t4, t5) { }

    template <class T1, class T2, class T3, class T4, class T5, class T6>
    SharedData (T1 t1, T2 t2, T3 t3, T4 t4, T5 t5, T6 t6)
        : m_value (t1, t2, t3, t4, t5, t6) { }

    template <class T1, class T2, class T3, class T4, class T5, class T6, class T7>
    SharedData (T1 t1, T2 t2, T3 t3, T4 t4, T5 t5, T6 t6, T7 t7) : m_value (t1, t2, t3, t4, t5, t6, t7) { }

    template <class T1, class T2, class T3, class T4, class T5, class T6, class T7, class T8>
    SharedData (T1 t1, T2 t2, T3 t3, T4 t4, T5 t5, T6 t6, T7 t7, T8 t8)
        : m_value (t1, t2, t3, t4, t5, t6, t7, t8) { }
    /** @} */

    SharedData (SharedData const&) = delete;
    SharedData& operator= (SharedData const&) = delete;

private:
    Value m_value;
    SharedMutexType m_mutex;
};

//------------------------------------------------------------------------------

/** Provides non-const access to the contents of a SharedData.
    This acquires a unique lock on the underlying mutex.
*/
template <class Data, class SharedMutexType>
class SharedData <Data, SharedMutexType>::Access
{
public:
    explicit Access (SharedData& state)
        : m_state (state)
        , m_lock (m_state.m_mutex)
        { }

    Access (Access const&) = delete;
    Access& operator= (Access const&) = delete;

    Data const& get () const { return m_state.m_value; }
    Data const& operator* () const { return get (); }
    Data const* operator-> () const { return &get (); }
    Data& get () { return m_state.m_value; }
    Data& operator* () { return get (); }
    Data* operator-> () { return &get (); }

private:
    SharedData& m_state;
    typename SharedData::LockGuardType m_lock;
};

//------------------------------------------------------------------------------

/** Provides const access to the contents of a SharedData.
    This acquires a shared lock on the underlying mutex.
*/
template <class Data, class SharedMutexType>
class SharedData <Data, SharedMutexType>::ConstAccess
{
public:
    /** Create a ConstAccess from the specified SharedData */
    explicit ConstAccess (SharedData const volatile& state)
        : m_state (const_cast <SharedData const&> (state))
        , m_lock (m_state.m_mutex)
        { }

    ConstAccess (ConstAccess const&) = delete;
    ConstAccess& operator= (ConstAccess const&) = delete;

    Data const& get () const { return m_state.m_value; }
    Data const& operator* () const { return get (); }
    Data const* operator-> () const { return &get (); }

private:
    SharedData const& m_state;
    typename SharedData::SharedLockGuardType m_lock;
};

//------------------------------------------------------------------------------

/** Provides const access to the contents of a SharedData.
    This acquires a shared lock on the underlying mutex.
*/
template <class Data, class SharedMutexType>
class SharedData <Data, SharedMutexType>::ConstUnlockedAccess
{
public:
    /** Create an UnlockedAccess from the specified SharedData */
    explicit ConstUnlockedAccess (SharedData const volatile& state)
        : m_state (const_cast <SharedData const&> (state))
        { }

    ConstUnlockedAccess (ConstUnlockedAccess const&) = delete;
    ConstUnlockedAccess& operator= (ConstUnlockedAccess const&) = delete;

    Data const& get () const { return m_state.m_value; }
    Data const& operator* () const { return get (); }
    Data const* operator-> () const { return &get (); }

private:
    SharedData const& m_state;
};

//------------------------------------------------------------------------------

/** Provides access to the contents of a SharedData.
    This acquires a shared lock on the underlying mutex.
*/
template <class Data, class SharedMutexType>
class SharedData <Data, SharedMutexType>::UnlockedAccess
{
public:
    /** Create an UnlockedAccess from the specified SharedData */
    explicit UnlockedAccess (SharedData& state)
        : m_state (state)
        { }

    UnlockedAccess (UnlockedAccess const&) = delete;
    UnlockedAccess& operator= (UnlockedAccess const&) = delete;

    Data const& get () const { return m_state.m_value; }
    Data const& operator* () const { return get (); }
    Data const* operator-> () const { return &get (); }
    Data& get () { return m_state.m_value; }
    Data& operator* () { return get (); }
    Data* operator-> () { return &get (); }

private:
    SharedData& m_state;
};

}

#endif
