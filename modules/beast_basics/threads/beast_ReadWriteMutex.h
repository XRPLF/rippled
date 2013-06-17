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

#ifndef BEAST_READWRITEMUTEX_BEASTHEADER
#define BEAST_READWRITEMUTEX_BEASTHEADER

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

/*============================================================================*/
/**
  Scoped read lock for ReadWriteMutex.

  @ingroup beast_concurrent
*/
template <class LockType>
struct GenericScopedReadLock : Uncopyable
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

/*============================================================================*/
/**
  Scoped write lock for ReadWriteMutex.

  @ingroup beast_concurrent
*/
template <class LockType>
struct GenericScopedWriteLock : Uncopyable
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

class ReadWriteMutex
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
