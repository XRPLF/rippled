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

#ifndef BEAST_SHAREDDATA_H_INCLUDED
#define BEAST_SHAREDDATA_H_INCLUDED

/*============================================================================*/
/**
  Structured access to a shared state.

  This template wraps an object containing members representing state
  information shared between multiple threads of execution, where any thread
  may need to read or write as needed. Synchronized access to the concurrent
  state is enforced at compile time through strongly typed accessor classes.
  This interface design facilitates source code pattern matching to find all
  areas where a concurrent state is accessed.

  There are three types of access:

  - ReadAccess

    Allows read access to the underlying object as `const`. ReadAccess may be
    granted to one or more threads simultaneously. If one or more threads have
    ReadAccess, requests to obtain WriteAccess are blocked.

  - WriteAccess

    Allows exclusive read/write access the underlying object. A WriteAccess
    request blocks until all existing ReadAccess and WriteAccess requests are
    released. While a WriteAccess exists, requests for ReadAccess will block.

  - UnlockedAccess

    Allows read access to the underlying object without using the lock. This
    can be helpful when designing concurrent structures through composition.
    It also makes it easier to search for places in code which use unlocked
    access.

  This code example demonstrates various forms of access to a SharedData:

  @code

  struct SharedData
  {
    int value1;
    String value2;
  };

  typedef SharedData <SharedData> SharedState;

  SharedState sharedState;

  void readExample ()
  {
    SharedState::ReadAccess state (sharedState);

    print (state->value1);   // read access
    print (state->value2);   // read access

    state->value1 = 42;      // write disallowed: compile error
  }

  void writeExample ()
  {
    SharedState::WriteAccess state (sharedState);

    state->value2 = "Label"; // write access
  }

  @endcode

  Forwarding constructors with up to eight parameters are provided. This lets
  you write constructors into the underlying data object. For example:

  @code

  struct SharedData
  {
    explicit SharedData (int numSlots)
    {
      m_array.reserve (numSlots);
    }

    std::vector <AudioSampleBuffer*> m_array;
  };

  // Construct SharedData with one parameter
  SharedData <SharedData> sharedState (16);

  @endcode

  @param Object The type of object to encapsulate.

  @warning Recursive calls are not supported. It is generally not possible for
            a thread of execution to acquire write access while it already has
  read access. Such an attempt will result in undefined behavior. Calling into
  unknown code while holding a lock can cause deadlock. See
  @ref CallQueue::queue().
*/
template <class Object>
class SharedData : Uncopyable
{
public:
    class ReadAccess;
    class WriteAccess;
    class UnlockedAccess;

    /** Create a concurrent state.

        Up to 8 parameters can be specified in the constructor. These parameters
        are forwarded to the corresponding constructor in Object. If no
        constructor in Object matches the parameter list, a compile error is
        generated.
    */
    /** @{ */
    SharedData () { }

    template <class T1>
    explicit SharedData (T1 t1)
        : m_obj (t1) { }

    template <class T1, class T2>
    SharedData (T1 t1, T2 t2)
        : m_obj (t1, t2) { }

    template <class T1, class T2, class T3>
    SharedData (T1 t1, T2 t2, T3 t3)
        : m_obj (t1, t2, t3) { }

    template <class T1, class T2, class T3, class T4>
    SharedData (T1 t1, T2 t2, T3 t3, T4 t4)
        : m_obj (t1, t2, t3, t4) { }

    template <class T1, class T2, class T3, class T4, class T5>
    SharedData (T1 t1, T2 t2, T3 t3, T4 t4, T5 t5)
        : m_obj (t1, t2, t3, t4, t5) { }

    template <class T1, class T2, class T3, class T4, class T5, class T6>
    SharedData (T1 t1, T2 t2, T3 t3, T4 t4, T5 t5, T6 t6)
        : m_obj (t1, t2, t3, t4, t5, t6) { }

    template <class T1, class T2, class T3, class T4, class T5, class T6, class T7>
    SharedData (T1 t1, T2 t2, T3 t3, T4 t4, T5 t5, T6 t6, T7 t7) : m_obj (t1, t2, t3, t4, t5, t6, t7) { }

    template <class T1, class T2, class T3, class T4, class T5, class T6, class T7, class T8>
    SharedData (T1 t1, T2 t2, T3 t3, T4 t4, T5 t5, T6 t6, T7 t7, T8 t8)
        : m_obj (t1, t2, t3, t4, t5, t6, t7, t8) { }
    /** @} */

private:
    typedef ReadWriteMutex ReadWriteMutexType;

    Object m_obj;
    ReadWriteMutexType m_mutex;
};

//------------------------------------------------------------------------------

/** Unlocked access to a SharedData.

    Use sparingly.
*/
template <class Object>
class SharedData <Object>::UnlockedAccess : Uncopyable
{
public:
    explicit UnlockedAccess (SharedData const& state)
        : m_state (state)
    {
    }

    Object const& getObject () const
    {
        return m_state.m_obj;
    }
    Object const& operator* () const
    {
        return getObject ();
    }
    Object const* operator-> () const
    {
        return &getObject ();
    }

private:
    SharedData const& m_state;
};

//------------------------------------------------------------------------------

/** Read only access to a SharedData */
template <class Object>
class SharedData <Object>::ReadAccess : Uncopyable
{
public:
    /** Create a ReadAccess from the specified SharedData */
    explicit ReadAccess (SharedData const volatile& state)
        : m_state (const_cast <SharedData const&> (state))
        , m_lock (m_state.m_mutex)
    {
    }

    /** Obtain a read only reference to Object */
    Object const& getObject () const
    {
        return m_state.m_obj;
    }

    /** Obtain a read only reference to Object */
    Object const& operator* () const
    {
        return getObject ();
    }

    /** Obtain a read only smart pointer to Object */
    Object const* operator-> () const
    {
        return &getObject ();
    }

private:
    SharedData const& m_state;
    ReadWriteMutexType::ScopedReadLockType m_lock;
};

//------------------------------------------------------------------------------

/** Read/write access to a SharedData */
template <class Object>
class SharedData <Object>::WriteAccess : Uncopyable
{
public:
    explicit WriteAccess (SharedData& state)
        : m_state (state)
        , m_lock (m_state.m_mutex)
    {
    }

    /** Obtain a read only reference to Object */
    Object const* getObject () const
    {
        return m_state.m_obj;
    }

    /** Obtain a read only reference to Object */
    Object const& operator* () const
    {
        return getObject ();
    }

    /** Obtain a read only smart pointer to Object */
    Object const* operator-> () const
    {
        return &getObject ();
    }

    /** Obtain a read/write pointer to Object */
    Object& getObject ()
    {
        return m_state.m_obj;
    }

    /** Obtain a read/write reference to Object */
    Object& operator* ()
    {
        return getObject ();
    }

    /** Obtain a read/write smart pointer to Object */
    Object* operator-> ()
    {
        return &getObject ();
    }

private:
    SharedData& m_state;
    ReadWriteMutexType::ScopedWriteLockType m_lock;
};

#endif
