//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Portions of this file are from JUCE.
    Copyright (c) 2013 - Raw Material Software Ltd.
    Please visit http://www.juce.com

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

#ifndef BEAST_MODULE_CORE_THREADS_CRITICALSECTION_H_INCLUDED
#define BEAST_MODULE_CORE_THREADS_CRITICALSECTION_H_INCLUDED

#include <beast/module/core/threads/ScopedLock.h>
#include <cstdint>

namespace beast {

//==============================================================================
/**
    A re-entrant mutex.

    A CriticalSection acts as a re-entrant mutex object. The best way to lock and unlock
    one of these is by using RAII in the form of a local ScopedLock object - have a look
    through the codebase for many examples of how to do this.

    @see ScopedLock, ScopedTryLock, ScopedUnlock, SpinLock, Thread
*/
class CriticalSection
{
public:
    //==============================================================================
    /** Creates a CriticalSection object. */
    CriticalSection() noexcept;

    CriticalSection (CriticalSection const&) = delete;
    CriticalSection& operator= (CriticalSection const&) = delete;

    /** Destructor.
        If the critical section is deleted whilst locked, any subsequent behaviour
        is unpredictable.
    */
    ~CriticalSection() noexcept;

    //==============================================================================
    /** Acquires the lock.

        If the lock is already held by the caller thread, the method returns immediately.
        If the lock is currently held by another thread, this will wait until it becomes free.

        It's strongly recommended that you never call this method directly - instead use the
        ScopedLock class to manage the locking using an RAII pattern instead.

        @see exit, tryEnter, ScopedLock
    */
    void enter() const noexcept;

    /** Attempts to lock this critical section without blocking.

        This method behaves identically to CriticalSection::enter, except that the caller thread
        does not wait if the lock is currently held by another thread but returns false immediately.

        @returns false if the lock is currently held by another thread, true otherwise.
        @see enter
    */
    bool tryEnter() const noexcept;

    /** Releases the lock.

        If the caller thread hasn't got the lock, this can have unpredictable results.

        If the enter() method has been called multiple times by the thread, each
        call must be matched by a call to exit() before other threads will be allowed
        to take over the lock.

        @see enter, ScopedLock
    */
    void exit() const noexcept;

    //==============================================================================
    /** Provides the type of scoped lock to use with a CriticalSection. */
    using ScopedLockType = GenericScopedLock <CriticalSection>;

    /** Provides the type of scoped unlocker to use with a CriticalSection. */
    using ScopedUnlockType = GenericScopedUnlock <CriticalSection>;

    /** Provides the type of scoped try-locker to use with a CriticalSection. */
    using ScopedTryLockType = GenericScopedTryLock <CriticalSection>;

    //--------------------------------------------------------------------------
    //
    // Boost concept compatibility
    // http://www.boost.org/doc/libs/1_54_0/doc/html/thread/synchronization.html#thread.synchronization.mutex_concepts
    //

    // BasicLockable
    inline void lock () const noexcept { enter (); }
    inline void unlock () const noexcept { exit (); }

    // Lockable
    inline bool try_lock () const noexcept { return tryEnter (); }

    //--------------------------------------------------------------------------

private:
    //==============================================================================
   #if BEAST_WINDOWS
    // To avoid including windows.h in the public Beast headers, we'll just allocate
    // a block of memory here that's big enough to be used internally as a windows
    // CRITICAL_SECTION structure.
    #if BEAST_64BIT
     std::uint8_t section[44];
    #else
     std::uint8_t section[24];
    #endif
   #else
    mutable pthread_mutex_t mutex;
   #endif
};

//==============================================================================
/**
    A class that can be used in place of a real CriticalSection object, but which
    doesn't perform any locking.

    This is currently used by some templated classes, and most compilers should
    manage to optimise it out of existence.

    @see CriticalSection, Array, SharedObjectArray
*/
class DummyCriticalSection
{
public:
    DummyCriticalSection() = default;
    DummyCriticalSection (DummyCriticalSection const&) = delete;
    DummyCriticalSection& operator= (DummyCriticalSection const&) = delete;
    ~DummyCriticalSection() = default;

    inline void enter() const noexcept          {}
    inline bool tryEnter() const noexcept       { return true; }
    inline void exit() const noexcept           {}

    //==============================================================================
    /** A dummy scoped-lock type to use with a dummy critical section. */
    struct ScopedLockType
    {
        ScopedLockType (const DummyCriticalSection&) noexcept {}
    };

    /** A dummy scoped-unlocker type to use with a dummy critical section. */
    using ScopedUnlockType = ScopedLockType;
};

//==============================================================================
/**
    Automatically locks and unlocks a CriticalSection object.

    Use one of these as a local variable to provide RAII-based locking of a CriticalSection.

    e.g. @code

    CriticalSection myCriticalSection;

    for (;;)
    {
        const ScopedLock myScopedLock (myCriticalSection);
        // myCriticalSection is now locked

        ...do some stuff...

        // myCriticalSection gets unlocked here.
    }
    @endcode

    @see CriticalSection, ScopedUnlock
*/
using ScopedLock = CriticalSection::ScopedLockType;

//==============================================================================
/**
    Automatically unlocks and re-locks a CriticalSection object.

    This is the reverse of a ScopedLock object - instead of locking the critical
    section for the lifetime of this object, it unlocks it.

    Make sure you don't try to unlock critical sections that aren't actually locked!

    e.g. @code

    CriticalSection myCriticalSection;

    for (;;)
    {
        const ScopedLock myScopedLock (myCriticalSection);
        // myCriticalSection is now locked

        ... do some stuff with it locked ..

        while (xyz)
        {
            ... do some stuff with it locked ..

            const ScopedUnlock unlocker (myCriticalSection);

            // myCriticalSection is now unlocked for the remainder of this block,
            // and re-locked at the end.

            ...do some stuff with it unlocked ...
        }

        // myCriticalSection gets unlocked here.
    }
    @endcode

    @see CriticalSection, ScopedLock
*/
using ScopedUnlock = CriticalSection::ScopedUnlockType;

//==============================================================================
/**
    Automatically tries to lock and unlock a CriticalSection object.

    Use one of these as a local variable to control access to a CriticalSection.

    e.g. @code
    CriticalSection myCriticalSection;

    for (;;)
    {
        const ScopedTryLock myScopedTryLock (myCriticalSection);

        // Unlike using a ScopedLock, this may fail to actually get the lock, so you
        // should test this with the isLocked() method before doing your thread-unsafe
        // action..
        if (myScopedTryLock.isLocked())
        {
           ...do some stuff...
        }
        else
        {
            ..our attempt at locking failed because another thread had already locked it..
        }

        // myCriticalSection gets unlocked here (if it was locked)
    }
    @endcode

    @see CriticalSection::tryEnter, ScopedLock, ScopedUnlock, ScopedReadLock
*/
using ScopedTryLock = CriticalSection::ScopedTryLockType;

} // beast

#endif   // BEAST_CRITICALSECTION_H_INCLUDED
