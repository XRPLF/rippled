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

#ifndef BEAST_CORE_STOPPABLE_H_INCLUDED
#define BEAST_CORE_STOPPABLE_H_INCLUDED

/** Provides an interface for stopping.

    This is the sequence of events involved in stopping:

    1.  stopAsync() [optional]

        This notifies the root Stoppable and all its children that a stop is
        requested.

    2.  stop()

        This first calls stopAsync(), and then blocks on each child Stoppable in
        the in the tree from the bottom up, until the Stoppable indicates it has
        stopped. This will usually be called from the main thread of execution
        when some external signal indicates that the process should stop. For
        example, an RPC 'stop' command, or a SIGINT POSIX signal.

    3.  onStop()

        This override is called for the root Stoppable and all its children when
        stopAsync() is called. Derived classes should cancel pending I/O and
        timers, signal that threads should exit, queue cleanup jobs, and perform
        any other necessary final actions in preparation for exit.

    4.  onChildrenStopped()

        This override is called when all the children have stopped. This informs
        the Stoppable that there should not be any more dependents making calls
        into its member functions. A Stoppable that has no children will still
        have this function called.

    5.  stopped()

        The derived class calls this function to inform the Stoppable API that
        it has completed the stop. This unblocks the caller of stop().

        For stoppables which are only considered stopped when all of their
        children have stopped, and their own internal logic indicates a stop, it
        will be necessary to perform special actions in onChildrenStopped(). The
        funtion areChildrenStopped() can be used after children have stopped,
        but before the Stoppable logic itself has stopped, to determine if the
        stoppable's logic is a true stop.
        
        Pseudo code for this process is as follows:

        @code

        // Returns `true` if derived logic has stopped.
        //
        // When the logic stops, logicProcessingStop() is no longer called.
        // If children are still active we need to wait until we get a
        // notification that the children have stopped.
        //
        bool logicHasStopped ();

        // Called when children have stopped
        void onChildrenStopped ()
        {
            // We have stopped when the derived logic stops and children stop.
            if (logicHasStopped)
                stopped();
        }

        // derived-specific logic that executes periodically
        void logicProcessingStep ()
        {
            // process
            // ...

            // now see if we've stopped
            if (logicHasStopped() && areChildrenStopped())
                stopped();
        }

        @endcode

        Derived class that manage one or more threads should typically notify
        those threads in onStop that they should exit. In the thread function,
        when the last thread is about to exit it would call stopped().

    @note A Stoppable may not be restarted.
*/
class Stoppable
{
public:
    /** Create the stoppable.
        A stoppable without a parent is a root stoppable.
        @param name A name used in log output.
        @param parent Optional parent of this stoppable.
    */
    /** @{ */
    Stoppable (char const* name, Stoppable& parent);
    explicit Stoppable (char const* name, Stoppable* parent = nullptr);
    /** @} */

    /** Destroy the stoppable.
        Undefined behavior results if the object is not stopped first.
        Stoppable objects should not be created and destroyed dynamically during
        the process lifetime. Rather, the set of stoppables should be static and
        well-defined after initialization. If the set of domain-specific objects
        which need to stop is dynamic, use a single parent Stoppable to manage
        those objects. For example, make an HTTP server implementation a
        Stoppable, rather than each of its active connections.
    */
    virtual ~Stoppable ();

    /** Notify a root stoppable and children to stop, and block until stopped.
        Has no effect if the stoppable was already notified.
        This blocks until the stoppable and all of its children have stopped.
        @param stream An optional Journal::Stream on which to log progress.

        Thread safety:
            Safe to call from any thread not associated with a Stoppable.
    */
    void stop (Journal::Stream stream = Journal::Stream());

    /** Notify a root stoppable and children to stop, without waiting.
        Has no effect if the stoppable was already notified.

        Thread safety:
            Safe to call from any thread at any time.
    */
    void stopAsync ();

    /** Returns `true` if the stoppable should stop.
        Call from the derived class to determine if a long-running operation
        should be canceled. This is not appropriate for either threads, or
        asynchronous I/O. For threads, use the thread-specific facilities
        available to inform the thread that it should exit. For asynchronous
        I/O, cancel all pending operations inside the onStop override.
        @see onStop

        Thread safety:
            Safe to call from any thread at any time.
    */
    bool isStopping ();
  
    /** Returns `true` if the stoppable has completed its stop.
        Thread safety:
            Safe to call from any thread at any time.
    */
    bool isStopped ();

    /** Returns `true` if all children have stopped.
        For stoppables without children, this returns `true` immediately
        after a stop notification is received.

        Thread safety:
            Safe to call from any thread at any time.
    */
    bool areChildrenStopped ();

    /** Called by derived classes to indicate that the stoppable has stopped.
        The derived class must call this either after isStopping returns `true`,
        or when onStop is called, or else the call to stop will never unblock.

        Thread safety:
            Safe to call from any thread at any time.
    */
    void stopped ();

    /** Override called when the stop notification is issued.

        The call is made on an unspecified, implementation-specific thread.
        onStop and onChildrenStopped will never be called concurrently, across
        all Stoppable objects descended from the same root, inclusive of the
        root.

        It is safe to call isStopping, isStopped, and  areChildrenStopped from
        within this function; The values returned will always be valid and never
        change during the callback.

        The default implementation simply calls stopped(). This is applicable
        when the Stoppable has a trivial stop operation (or no stop operation),
        and we are merely using the Stoppable API to position it as a dependency
        of some parent service.

        Thread safety:
            May not block for long periods.
            Guaranteed only to be called once.
            Must be safe to call from any thread at any time.
    */
    virtual void onStop ();

    /** Override called when all children have stopped.

        The call is made on an unspecified, implementation-specific thread.
        onStop and onChildrenStopped will never be called concurrently, across
        all Stoppable objects descended from the same root, inclusive of the
        root.
        
        It is safe to call isStopping, isStopped, and  areChildrenStopped from
        within this function; The values returned will always be valid and never
        change during the callback.

        The default implementation does nothing.

        Thread safety:
            May not block for long periods.
            Guaranteed only to be called once.
            Must be safe to call from any thread at any time.
    */
    virtual void onChildrenStopped ();

private:
    struct Child;
    typedef LockFreeStack <Child> Children;

    struct Child : Children::Node
    {
        Child (Stoppable* stoppable_) : stoppable (stoppable_)
        {
        }

        Stoppable* stoppable;
    };

    void stopAsyncRecursive ();
    void stopRecursive (Journal::Stream stream);

    char const* m_name;
    bool m_root;
    Child m_child;
    Children m_children;

    // Flag that we called stop. This is for diagnostics.
    bool m_calledStop;

    // Atomic flag to make sure we only call stopAsync once.
    Atomic <int> m_calledStopAsync;

    // Flag that this service stopped. Never goes back to false.
    bool volatile m_stopped;

    // Flag that all children have stopped (recursive). Never goes back to false.
    bool volatile m_childrenStopped;

    // stop() blocks on this event until stopped() is called.
    WaitableEvent m_stoppedEvent;
};

#endif
