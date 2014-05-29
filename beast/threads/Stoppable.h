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

#ifndef BEAST_THREADS_STOPPABLE_H_INCLUDED
#define BEAST_THREADS_STOPPABLE_H_INCLUDED

#include <beast/Atomic.h>
#include <beast/intrusive/LockFreeStack.h>
#include <beast/utility/Journal.h>

#include <beast/threads/WaitableEvent.h>

namespace beast {

class RootStoppable;

/** Provides an interface for starting and stopping.

    A common method of structuring server or peer to peer code is to isolate
    conceptual portions of functionality into individual classes, aggregated
    into some larger "application" or "core" object which holds all the parts.
    Frequently, these components are dependent on each other in unavoidably
    complex ways. They also often use threads and perform asynchronous i/o
    operations involving sockets or other operating system objects. The process
    of starting and stopping such a system can be complex. This interface
    provides a set of behaviors for ensuring that the start and stop of a
    composite application-style object is well defined.

    Upon the initialization of the composite object these steps are peformed:

    1.  Construct sub-components.

        These are all typically derived from Stoppable. There can be a deep
        hierarchy: Stoppable objects may themselves have Stoppable child
        objects. This captures the relationship of dependencies.

    2.  prepare()

        Because some components may depend on others, preparatory steps require
        that all objects be first constructed. The prepare step calls all
        Stoppable objects in the tree starting from the leaves and working up
        to the root. In this stage we are guaranteed that all objects have been
        constructed and are in a well-defined state.

    3.  onPrepare()

        This override is called for all Stoppable objects in the hierarchy
        during the prepare stage. Objects are called from the bottom up.
        It is guaranteed that all child Stoppable objects have already been
        prepared when this is called.

    4.  start()

        At this point all sub-components have been constructed and prepared,
        so it should be safe for them to be started. While some Stoppable
        objects may do nothing in their start function, others will start
        threads or call asynchronous i/o initiating functions like timers or
        sockets.

    5.  onStart()

        This override is called for all Stoppable objects in the hierarchy
        during the start stage. Objects are called from the bottom up.
        It is guaranteed that all child Stoppable objects have already been
        started when this is called.

    This is the sequence of events involved in stopping:

    6.  stopAsync() [optional]

        This notifies the root Stoppable and all its children that a stop is
        requested.

    7.  stop()

        This first calls stopAsync(), and then blocks on each child Stoppable in
        the in the tree from the bottom up, until the Stoppable indicates it has
        stopped. This will usually be called from the main thread of execution
        when some external signal indicates that the process should stop. For
        example, an RPC 'stop' command, or a SIGINT POSIX signal.

    8.  onStop()

        This override is called for the root Stoppable and all its children when
        stopAsync() is called. Derived classes should cancel pending I/O and
        timers, signal that threads should exit, queue cleanup jobs, and perform
        any other necessary final actions in preparation for exit.

    9.  onChildrenStopped()

        This override is called when all the children have stopped. This informs
        the Stoppable that there should not be any more dependents making calls
        into its member functions. A Stoppable that has no children will still
        have this function called.

    10. stopped()

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
/** @{ */
class Stoppable
{
protected:
    Stoppable (char const* name, RootStoppable& root);

public:
    /** Create the Stoppable. */
    Stoppable (char const* name, Stoppable& parent);

    /** Destroy the Stoppable. */
    virtual ~Stoppable ();

    /** Returns `true` if the stoppable should stop. */
    bool isStopping () const;

    /** Returns `true` if the requested stop has completed. */
    bool isStopped () const;

    /** Returns `true` if all children have stopped. */
    bool areChildrenStopped () const;

    /** Called by derived classes to indicate that the stoppable has stopped. */
    void stopped ();

    /** Override called during preparation.
        Since all other Stoppable objects in the tree have already been
        constructed, this provides an opportunity to perform initialization which
        depends on calling into other Stoppable objects.
        This call is made on the same thread that called prepare().
        The default implementation does nothing.
        Guaranteed to only be called once.
    */
    virtual void onPrepare ();

    /** Override called during start. */
    virtual void onStart ();

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
    friend class RootStoppable;

    struct Child;
    typedef LockFreeStack <Child> Children;

    struct Child : Children::Node
    {
        Child (Stoppable* stoppable_) : stoppable (stoppable_)
        {
        }

        Stoppable* stoppable;
    };

    void prepareRecursive ();
    void startRecursive ();
    void stopAsyncRecursive ();
    void stopRecursive (Journal journal);

protected:
    char const* m_name;
    RootStoppable& m_root;
    Child m_child;
    Atomic <int> m_started;
    bool volatile m_stopped;
    bool volatile m_childrenStopped;
    Children m_children;
    WaitableEvent m_stoppedEvent;
};

//------------------------------------------------------------------------------

class RootStoppable : public Stoppable
{
public:
    explicit RootStoppable (char const* name);

    ~RootStoppable ();

    bool isStopping() const;

    /** Prepare all contained Stoppable objects.
        This calls onPrepare for all Stoppable objects in the tree.
        Calls made after the first have no effect.
        Thread safety:
            May be called from any thread.
    */
    void prepare ();

    /** Start all contained Stoppable objects.
        The default implementation does nothing.
        Calls made after the first have no effect.
        Thread safety:
            May be called from any thread.
    */
    void start ();

    /** Notify a root stoppable and children to stop, and block until stopped.
        Has no effect if the stoppable was already notified.
        This blocks until the stoppable and all of its children have stopped.
        Undefined behavior results if stop() is called without a previous call
        to start().
        Thread safety:
            Safe to call from any thread not associated with a Stoppable.
    */
    void stop (Journal journal = Journal());

    /** Notify a root stoppable and children to stop, without waiting.
        Has no effect if the stoppable was already notified.

        Thread safety:
            Safe to call from any thread at any time.
    */
    void stopAsync ();

private:
    Atomic <int> m_prepared;
    Atomic <int> m_calledStop;
    Atomic <int> m_calledStopAsync;
};
/** @} */

}

#endif
