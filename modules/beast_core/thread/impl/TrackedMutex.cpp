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

namespace beast {
namespace detail {

// Example:
//
// m_mutex[2] @ beast_deadlineTimer.cpp(25)
//
String detail::TrackedMutexBasics::createName (String name,
    char const* fileName, int lineNumber, int instanceNumber)
{
    return name +
        ((instanceNumber > 1) ?
            (String ("[") + String::fromNumber (instanceNumber) + "] (") : " (") +
                Debug::getFileNameFromPath (fileName) + "," +
                String::fromNumber (lineNumber) + ")";
}

Atomic <int> TrackedMutexBasics::lastThreadId;

ThreadLocalValue <TrackedMutexBasics::PerThreadDataStorage> TrackedMutexBasics::threadLocal;

TrackedMutexBasics::PerThreadData::PerThreadData ()
    : id (++lastThreadId)
{
}

TrackedMutexBasics::PerThreadData& TrackedMutexBasics::getPerThreadData ()
{
    PerThreadData& thread (*reinterpret_cast <PerThreadData*>(&threadLocal.get ()));

    // Manually call the constructor with placement new if needed
    if (! thread.id)
    {
        new (&thread) PerThreadData ();
        bassert (thread.id != 0);
    }

    return thread;
}

CriticalSection& TrackedMutexBasics::getGlobalMutex ()
{
    static CriticalSection mutex;
    return mutex;
}

TrackedMutexBasics::Lists& TrackedMutexBasics::getLists ()
{
    static Lists lists;
    return lists;
}

//------------------------------------------------------------------------------

} // detail

//==============================================================================

TrackedMutex::Record::Record (String mutexName,
    String threadName, String sourceLocation)
    : m_mutexName (mutexName)
    , m_threadName (threadName)
    , m_sourceLocation (sourceLocation)
{
}

TrackedMutex::Record::Record () noexcept
{
}

TrackedMutex::Record::Record (Record const& other) noexcept
    : m_mutexName (other.m_mutexName)
    , m_threadName (other.m_threadName)
    , m_sourceLocation (other.m_sourceLocation)
{
}

TrackedMutex::Record& TrackedMutex::Record::operator= (Record const& other) noexcept
{
    m_mutexName = other.m_mutexName;
    m_threadName = other.m_threadName;
    m_sourceLocation = other.m_sourceLocation;
    return *this;
}

bool TrackedMutex::Record::isNull () const noexcept
{
    return m_mutexName == "";
}

bool TrackedMutex::Record::isNotNull () const noexcept
{
    return m_mutexName != "";
}

TrackedMutex::Record::operator bool() const noexcept
{
    return isNotNull ();
}

String TrackedMutex::Record::getMutexName () const noexcept
{
    return m_mutexName;
}

String TrackedMutex::Record::getThreadName () const noexcept
{
    return m_threadName;
}

String TrackedMutex::Record::getSourceLocation () const noexcept
{
    return m_sourceLocation;
}

//------------------------------------------------------------------------------

TrackedMutex::Agent::Agent (detail::TrackedMutexBasics::PerThreadData* thread)
    : m_thread (thread)
    , m_threadName (thread->threadName)
{
}

TrackedMutex::Agent::Agent () noexcept
    : m_thread (nullptr)
{
}

TrackedMutex::Agent::Agent (Agent const& other) noexcept
    : m_thread (other.m_thread)
    , m_threadName (other.m_threadName)
    , m_blocked (other.m_blocked)
{
}

TrackedMutex::Agent& TrackedMutex::Agent::operator= (Agent const& other) noexcept
{
    m_thread = other.m_thread;
    m_threadName = other.m_threadName;
    m_blocked = other.m_blocked;
    return *this;
}

bool TrackedMutex::Agent::isNull () const noexcept
{
    return m_thread == nullptr;
}

bool TrackedMutex::Agent::isNotNull () const noexcept
{
    return m_thread != nullptr;
}

TrackedMutex::Agent::operator bool() const noexcept
{
    return isNotNull ();
}

String TrackedMutex::Agent::getThreadName () const noexcept
{
    return m_threadName;
}

TrackedMutex::Record TrackedMutex::Agent::getBlockedRecord () const noexcept
{
    return m_blocked;
}

bool TrackedMutex::Agent::getLockedList (Array <Record>& output)
{
    bassert (isNotNull ());

    output.clearQuick ();

    typedef detail::TrackedMutexBasics::ThreadLockList ListType;

    {
        CriticalSection::ScopedLockType lock (m_thread->mutex);

        ListType const& list (m_thread->list);

        output.ensureStorageAllocated (list.size ());

        for (ListType::const_iterator iter = list.begin ();
            iter != list.end (); ++iter)
        {
            TrackedMutex const& mutex = *iter;
            {
                TrackedMutex::SharedState::ConstAccess state (mutex.m_state);
                output.add (state->owner);
            }
        }
    }

    return output.size () > 0;
}

//------------------------------------------------------------------------------

TrackedMutex::State::State ()
    : thread (nullptr)
{
}

//------------------------------------------------------------------------------

TrackedMutex::TrackedMutex (String const& name) noexcept
    : m_name (name)
    , m_count (0)
{
}

String TrackedMutex::getName () const noexcept
{
    return m_name;
}

TrackedMutex::Record TrackedMutex::getOwnerRecord () const noexcept
{
    {
        SharedState::ConstAccess state (m_state);
        return state->owner;
    }
}

TrackedMutex::Agent TrackedMutex::getOwnerAgent () const noexcept
{
    {
        SharedState::ConstAccess state (m_state);
        if (state->thread != nullptr)
            return Agent (state->thread);
    }

    return Agent ();
}

//------------------------------------------------------------------------------

void TrackedMutex::generateGlobalBlockedReport (StringArray& report)
{
    report.clear ();

    {
        CriticalSection::ScopedLockType lock (
            detail::TrackedMutexBasics::getGlobalMutex ());

        typedef detail::TrackedMutexBasics::GlobalThreadList ListType;

        ListType const& list (detail::TrackedMutexBasics::getLists ().allThreads);

        for (ListType::const_iterator iter = list.begin (); iter != list.end (); ++iter)
        {
            detail::TrackedMutexBasics::PerThreadData const* thread (
                &(*iter));

            typedef detail::TrackedMutexBasics::ThreadLockList LockedList;
            LockedList const& owned (thread->list);

            if (thread->blocked)
            {
                String s;
                s << thread->threadName << " blocked on " <<
                     thread->blocked->getName () << " at " <<
                     thread->sourceLocation;
                if (owned.size () > 0)
                    s << " and owns these locks:";
                report.add (s);
            }
            else if (owned.size () > 0)
            {
                String s;
                s << thread->threadName << " owns these locks:";
                report.add (s);
            }

            if (owned.size () > 0)
            {
                for (LockedList::const_iterator iter = owned.begin (); iter != owned.end (); ++iter)
                {
                    String s;
                    TrackedMutex const& mutex (*iter);
                    TrackedMutex::SharedState::ConstAccess state (mutex.m_state);
                    s << "      " << mutex.getName () <<
                         " from " << state->owner.getSourceLocation ();
                    report.add (s);
                }
            }
        }
    }
}

//------------------------------------------------------------------------------

// Called before we attempt to acquire the mutex.
//
void TrackedMutex::block (char const* fileName, int lineNumber) const noexcept
{
    // Get calling thread data.
    detail::TrackedMutexBasics::PerThreadData& thread
        (detail::TrackedMutexBasics::getPerThreadData ());

    ++thread.refCount;

    String const sourceLocation (makeSourceLocation (fileName, lineNumber));

    {
        // Take a global lock.
        CriticalSection::ScopedLockType globalLock (
            detail::TrackedMutexBasics::getGlobalMutex ());

        {
            // Take a thread lock.
            CriticalSection::ScopedLockType threadLock (thread.mutex);

            // Set the thread's blocked record
            thread.blocked = this;
            thread.threadName = makeThreadName (thread);
            thread.sourceLocation = sourceLocation;

            // Add this thread to threads list.
            if (thread.refCount == 1)
                detail::TrackedMutexBasics::getLists().allThreads.push_back (thread);
        }
    }
}

//------------------------------------------------------------------------------

// Called after we already have ownership of the mutex
//
void TrackedMutex::acquired (char const* fileName, int lineNumber) const noexcept
{
    // Retrieve the per-thread data for the calling thread.
    detail::TrackedMutexBasics::PerThreadData& thread
        (detail::TrackedMutexBasics::getPerThreadData ());

    // If this goes off it means block() wasn't called.
    bassert (thread.refCount > 0);

    ++m_count;

    // Take ownership on the first count.
    if (m_count == 1)
    {
        // Thread is a new owner of the mutex.
        String const sourceLocation (makeSourceLocation (fileName, lineNumber));
        String const threadName (makeThreadName (thread));

        {
            // Take a global lock.
            CriticalSection::ScopedLockType globalLock (
                detail::TrackedMutexBasics::getGlobalMutex ());

            {
                // Take a state lock.
                SharedState::Access state (m_state);

                // Set the mutex ownership record
                state->owner = Record (getName (), threadName, sourceLocation);
                state->thread = &thread;

                {
                    // Take a thread lock.
                    CriticalSection::ScopedLockType threadLock (thread.mutex);

                    // Add the mutex to the thread's list.
                    thread.list.push_back (const_cast <TrackedMutex&>(*this));

                    // Unblock the thread record.
                    thread.blocked = nullptr;
                    thread.sourceLocation = "";
                }
            }
        }
    }
    else
    {
        // Thread already had ownership of the mutex.
        bassert (SharedState::ConstUnlockedAccess (m_state)->thread == &thread);

        // If this goes off it means we counted wrong.
        bassert (thread.refCount >= m_count);
    }
}

//------------------------------------------------------------------------------

void TrackedMutex::release () const noexcept
{
    // If this goes off it means we don't own the mutex!
    bassert (m_count > 0);

    // Retrieve the per-thread data for the calling thread.
    detail::TrackedMutexBasics::PerThreadData& thread
        (detail::TrackedMutexBasics::getPerThreadData ());

    // If this goes off it means we counted wrong.
    bassert (thread.refCount >= m_count);

    --m_count;
    --thread.refCount;

    // Give up ownership when the count drops to zero.
    if (m_count == 0)
    {
        // Take the global mutex
        CriticalSection::ScopedLockType globalLock (
            detail::TrackedMutexBasics::getGlobalMutex ());

        {
            // Take the mutex' state lock
            SharedState::Access state (m_state);

            // Clear the mutex ownership record
            state->owner = Record ();
            state->thread = nullptr;

            {
                // Take the thread mutex
                CriticalSection::ScopedLockType threadLock (thread.mutex);

                // Remove this mutex from the list of the thread's owned locks.
                thread.list.erase (thread.list.iterator_to (
                    const_cast <TrackedMutex&>(*this)));

                // Remove this thread from the threads list.
                if (thread.refCount == 0)
                {
                    typedef detail::TrackedMutexBasics::GlobalThreadList ListType;
                    ListType& list (detail::TrackedMutexBasics::getLists().allThreads);
                    list.erase (list.iterator_to (thread));
                }
            }
        }
    }
}

//------------------------------------------------------------------------------

String TrackedMutex::makeThreadName (
    detail::TrackedMutexBasics::PerThreadData const& thread) noexcept
{
    String threadName;
    Thread const* const currentThread (Thread::getCurrentThread ());
    if (currentThread != nullptr)
        threadName = currentThread->getThreadName ();
    threadName = threadName + "[" + String::fromNumber (thread.id) + "]";
    return threadName;
}

String TrackedMutex::makeSourceLocation (char const* fileName, int lineNumber) noexcept
{
    String const sourceLocation (Debug::getFileNameFromPath (fileName, 1) + "(" +
        String::fromNumber (lineNumber) + ")");

    return sourceLocation;
}

} // beast
