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

#ifndef BEAST_CORE_THREAD_IMPL_TRACKEDMUTEX_H_INCLUDED
#define BEAST_CORE_THREAD_IMPL_TRACKEDMUTEX_H_INCLUDED

/** Common types and member functions for a TrackedMutex */
class TrackedMutex
    : public detail::TrackedMutexBasics::ThreadLockList::Node
{
public:
    class Agent;
    class Location;

    //--------------------------------------------------------------------------

    /** A triplet identifying a mutex, a thread, and source code location.
    */
    class Record
    {
    public:
        Record () noexcept;
        Record (Record const& other) noexcept;
        Record& operator= (Record const& other) noexcept;

        bool isNull () const noexcept;
        bool isNotNull () const noexcept;
        explicit operator bool() const noexcept;

        /** Returns the name of the mutex.
            Since the Mutex may not exist after the Record record is
            created, we only provide a String, which is always valid.
        */
        String getMutexName () const noexcept;

        /** Returns the name of the associated thread.
            The name is generated at the time the record is created,
            and might have changed since that time, or may no longer exist.
        */
        String getThreadName () const noexcept;

        /** Returns the position within the source code.
            This will either be the place a lock was acquired, or the place
            where a thread is trying to acquire a lock. The vaue is only
            meaningful at the time the Record is created. Since then, the
            thread may have changed its state.
        */
        String getSourceLocation () const noexcept;

    private:
        friend class TrackedMutex;

        Record (String mutexName, String threadName, String sourceLocation);

        String m_mutexName;
        String m_threadName;
        String m_sourceLocation;
    };

    //--------------------------------------------------------------------------

    /** Describes a thread that can acquire mutexes. */
    class Agent
    {
    public:
        Agent () noexcept;
        Agent (Agent const& other) noexcept;
        Agent& operator= (Agent const& other) noexcept;

        bool isNull () const noexcept;
        bool isNotNull () const noexcept;
        explicit operator bool() const noexcept;

        /** Returns the name of the thread.
            The name is generated at the time the Agent record is created,
            and might have changed since that time.
        */
        String getThreadName () const noexcept;

        /** Returns a Record indicating where the thread is blocked on a mutex.
            If the thread is not blocked, a null Record is returned.
            The value is only meaningful at the moment of the call as conditions
            can change.
        */
        Record getBlockedRecord () const noexcept;

        /** Retrieve a list of other locks that this thread holds.
            Each lock is represented by a Location indicating the place
            The value is only meaningful at the moment of the call as conditions
            can change.
            @return `true` if the list is not empty.
        */
        bool getLockedList (Array <Record>& list);

    private:
        friend class TrackedMutex;
        explicit Agent (detail::TrackedMutexBasics::PerThreadData* thread);

        detail::TrackedMutexBasics::PerThreadData* m_thread;
        String m_threadName;
        Record m_blocked;
    };

    //--------------------------------------------------------------------------

    /** Retrieve the name of this mutex.
        Thread safety: May be called from any thread.
    */
    String getName () const noexcept;

    /** Retrieve a Record for the current owner.
        It is only valid at the one instant in time, as the person holding it
        might have released it shortly afterwards. If there is no owner,
        a null Record is returned.
    */
    Record getOwnerRecord () const noexcept;

    /** Retrieve the Agent for the current owner.
        It is only valid at the one instant in time, as the person holding it
        might have released it shortly afterwards. If there is no owner,
        a null Agent is returned.
    */
    Agent getOwnerAgent () const noexcept;

    /** Produce a report on the state of all blocked threads. */
    static void generateGlobalBlockedReport (StringArray& report);

protected:
    static String makeThreadName (detail::TrackedMutexBasics::PerThreadData const&) noexcept;
    static String makeSourceLocation (char const* fileName, int lineNumber) noexcept;

    TrackedMutex (String const& name) noexcept;
    void block (char const* fileName, int lineNumber) const noexcept;
    void acquired (char const* fileName, int lineNumber) const noexcept;
    void release () const noexcept;

private:
    struct State
    {
        State ();
        Record owner;
        detail::TrackedMutexBasics::PerThreadData* thread;
    };

    typedef SharedData <State> SharedState;

    String const m_name;
    int mutable m_count;
    SharedState mutable m_state;
};

#endif
