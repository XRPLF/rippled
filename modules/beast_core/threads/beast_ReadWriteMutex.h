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

#ifndef BEAST_READWRITEMUTEX_H_INCLUDED
#define BEAST_READWRITEMUTEX_H_INCLUDED

/*============================================================================*/
/**
  Multiple consumer, single producer (MCSP) synchronization.

  This is an optimized lock for the multiple reader, single writer
  scenario. It provides only a subset of features of the more general
  traditional read/write lock. Specifically, these rules apply:

  - A caller cannot hold a read lock while acquiring a write lock.

  - Write locks are only recursive with respect to write locks.

  - Read locks are only recursive with respect to read locks.

  - A write lock cannot be downgraded.

  - Writes are preferenced over reads.

  For real-time applications, these restrictions are often not an issue.

  The implementation is wait-free in the fast path: acquiring read access
  for a lock without contention - just one interlocked increment!

  @class ReadWriteMutex
  @ingroup beast_concurrent
*/

//------------------------------------------------------------------------------

/**
  Scoped read lock for ReadWriteMutex.

  @ingroup beast_concurrent
*/
template <class LockType>
struct GenericScopedReadLock : public Uncopyable
{
    inline explicit GenericScopedReadLock (LockType const& lock) noexcept
:
    m_lock (lock)
    {
        m_lock.enterRead ();
    }

    inline ~GenericScopedReadLock () noexcept
    {
        m_lock.exitRead ();
    }

private:
    LockType const& m_lock;
};

//------------------------------------------------------------------------------

/**
  Scoped write lock for ReadWriteMutex.

  @ingroup beast_concurrent
*/
template <class LockType>
struct GenericScopedWriteLock : public Uncopyable
{
    inline explicit GenericScopedWriteLock (LockType const& lock) noexcept
:
    m_lock (lock)
    {
        m_lock.enterWrite ();
    }

    inline ~GenericScopedWriteLock () noexcept
    {
        m_lock.exitWrite ();
    }

private:
    LockType const& m_lock;
};

//------------------------------------------------------------------------------

class BEAST_API ReadWriteMutex
{
public:
    /** Provides the type of scoped read lock to use with a ReadWriteMutex. */
    typedef GenericScopedReadLock <ReadWriteMutex> ScopedReadLockType;

    /** Provides the type of scoped write lock to use with a ReadWriteMutex. */
    typedef GenericScopedWriteLock <ReadWriteMutex> ScopedWriteLockType;

    /** Create a ReadWriteMutex */
    ReadWriteMutex () noexcept;

    /** Destroy a ReadWriteMutex

        If the object is destroyed while a lock is held, the result is
        undefined behavior.
    */
    ~ReadWriteMutex () noexcept;

    /** Acquire a read lock.

        This is recursive with respect to other read locks. Calling this while
        holding a write lock is undefined.
    */
    void enterRead () const noexcept;

    /** Release a previously acquired read lock */
    void exitRead () const noexcept;

    /** Acquire a write lock.

        This is recursive with respect to other write locks. Calling this while
        holding a read lock is undefined.
    */
    void enterWrite () const noexcept;

    /** Release a previously acquired write lock */
    void exitWrite () const noexcept;

private:
    CriticalSection m_mutex;

    mutable CacheLine::Padded <AtomicCounter> m_writes;
    mutable CacheLine::Padded <AtomicCounter> m_readers;
};

#endif
