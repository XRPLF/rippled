//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2015 Ripple Labs Inc.

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

#ifndef RIPPLE_CORE_STOPPABLE_H_INCLUDED
#define RIPPLE_CORE_STOPPABLE_H_INCLUDED

#include <ripple/beast/core/LockFreeStack.h>
#include <ripple/beast/utility/Journal.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace ripple {

class RootStoppable;

/** Provides an interface for stopping.

    A common method of structuring server or peer to peer code is to isolate
    conceptual portions of functionality into individual classes, aggregated
    into some larger "application" or "core" object which holds all the parts.
    Frequently, these components are dependent on each other in unavoidably
    complex ways. They also often use threads and perform asynchronous i/o
    operations involving sockets or other operating system objects. The process
    of stopping such a system can be complex. This interface
    provides a set of behaviors for ensuring that the stop of a
    composite application-style object is well defined.

    Upon the initialization of the composite object these steps are performed:

    1.  Construct sub-components.

        These are all typically derived from Stoppable. There can be a deep
        hierarchy: Stoppable objects may themselves have Stoppable child
        objects. This captures the relationship of dependencies.

    This is the sequence of events involved in stopping:

    4.  stopAsync() [optional]

        This notifies the root Stoppable and all its children that a stop is
        requested.

    5.  stop()

        This first calls stopAsync(), and then blocks on each child Stoppable in
        the in the tree from the bottom up, until the Stoppable indicates it has
        stopped. This will usually be called from the main thread of execution
        when some external signal indicates that the process should stop. For
        example, an RPC 'stop' command, or a SIGINT POSIX signal.

    6.  onStop()

        This override is called for the root Stoppable and all its children when
        stopAsync() is called. Derived classes should cancel pending I/O and
        timers, signal that threads should exit, queue cleanup jobs, and perform
        any other necessary final actions in preparation for exit.

        Objects are called parent first.

        Derived class that manage one or more threads should typically notify
        those threads in onStop that they should exit. In the thread function,
        when the last thread is about to exit it would call stopped().

    The form of the Stoppable tree in the rippled application evolves as
    the source code changes and reacts to new demands. As of July in 2020
    the Stoppable tree had this form:

    @code

                                   Application
                                        |
                   +--------------------+--------------------+
                   |                    |                    |
              LoadManager          SHAMapStore       NodeStoreScheduler
                                                             |
                                                         JobQueue
                                                             |
        +------+---------+--------+----------+---+-----------+--+---+
        |      |         |        |          |   |              |   |
        |  NetworkOPs    |   InboundLedgers  |   |      OrderbookDB |
        |                |                   |  GRPCServer          |
        |                |                   |                  Database
     Overlay    InboundTransactions     LedgerMaster                |
        |                                    |                      |
    PeerFinder                          LedgerCleaner          TaskQueue

    @endcode
*/
/** @{ */
class Stoppable
{
protected:
    Stoppable(std::string name, RootStoppable& root);

public:
    /** Create the Stoppable. */
    Stoppable(std::string name, Stoppable& parent);

    /** Destroy the Stoppable. */
    virtual ~Stoppable() = default;

    /** Returns `true` if the stoppable should stop. */
    bool
    isStopping() const;

    /** Override called when the stop notification is issued.

        The call is made on an unspecified, implementation-specific thread.
        onStop will never be called concurrently, across
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
    virtual void
    onStop() = 0;

private:
    friend class RootStoppable;

    struct Child;
    using Children = beast::LockFreeStack<Child>;

    struct Child : Children::Node
    {
        Child(Stoppable* stoppable_) : stoppable(stoppable_)
        {
        }

        Stoppable* stoppable;
    };

    void
    stopAsyncRecursive(beast::Journal j);

    std::string m_name;
    RootStoppable& m_root;
    Child m_child;
    Children m_children;
};

//------------------------------------------------------------------------------

class RootStoppable : public Stoppable
{
public:
    explicit RootStoppable(std::string name);

    virtual ~RootStoppable() = default;

    bool
    isStopping() const;

    /** Notify a root stoppable and children to stop, and block until stopped.
        Has no effect if the stoppable was already notified.
        This blocks until the stoppable and all of its children have stopped.
        Thread safety:
            Safe to call from any thread not associated with a Stoppable.
    */
    void
    stop(beast::Journal j);

    void
    onStop() override
    {
    }

private:
    // TODO [C++20]: Use std::atomic_flag instead.
    std::atomic<bool> stopEntered_{false};
    std::mutex m_;
};
/** @} */

}  // namespace ripple

#endif
