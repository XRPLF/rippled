//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_FRAME_SERVICE_H_INCLUDED
#define RIPPLE_FRAME_SERVICE_H_INCLUDED

#include "../../../beast/beast/utility/Journal.h"

namespace ripple
{

using namespace beast;

/** Abstraction for organizing partitioned support code.

    The main thing a service can do, is to stop. Once it stops it cannot be
    reused, it can only be destroyed. This interface is used to coordinate
    the complex activities required for a clean exit in the presence of
    pending asynchronous i/o and multiple theads.

    This is the sequence of events involved in stopping a service:
 
    1.  serviceStopAsync() [optional]

        This notifies the Service and all its children that a stop is requested.

    2.  serviceStop ()

        This first calls serviceStopAsync(), and then blocks on each Service
        in the tree from the bottom up, until the Service indicates it has
        stopped. This will usually be called from the main thread of execution
        when some external signal indicates that the process should stop.
        FOr example, an RPC 'stop' command, or a SIGINT POSIX signal.

    3.  onServiceStop ()

        This is called for the root Service and all its children when a stop
        is requested. Derived classes should cancel pending I/O and timers,
        signal that threads should exit, queue cleanup jobs, and perform any
        other necessary clean up in preparation for exit.

    4.  onServiceChildrenStopped ()

        When all the children of a service have stopped, this will be called.
        This informs the Service that there should not be any more dependents
        making calls into the derived class member functions. A Service that
        has no children will have this function called immediately.

    5.  serviceStopped ()

        The derived class calls this function to inform the Service API that
        it has completed the stop. This unblocks the caller of serviceStop().

        For services which are only considered stopped when all of their children
        have stopped, and their own internal logic indicates a stop, it will be
        necessary to perform special actions in onServiceChildrenStopped(). The
        funtion areServiceChildrenStopped() can be used after children have
        stopped, but before the Service logic itself has stopped, to determine
        if the stopped service logic is a true stop.
        
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
        void onServiceChildrenStopped ()
        {
            // We have stopped when the derived logic stops and children stop.
            if (logicHasStopped)
                serviceStopped();
        }

        // derived-specific logic that executes periodically
        void logicProcessingStep ()
        {
            // do the step
            // ...

            // now see if we've stopped
            if (logicHasStopped() && areServiceChildrenStopped())
                serviceStopped();
        }

        @endcode
*/
class Service
{
public:
    /** Create a service.
        Services are always created in a non-stopped state.
        A service without a parent is a root service.
    */
    /** @{ */
    explicit Service (char const* name);
    Service (char const* name, Service* parent);
    Service (char const* name, Service& parent);
    /** @} */

    /** Destroy the service.
        Undefined behavior results if the service is not first stopped.

        In general, services are not allowed to be created and destroyed
        dynamically. The set of services should be static at some point
        after the initialization of the process. If you need a dynamic
        service, consider having a static Service which marshals service
        calls to a second custom interface.
    */
    virtual ~Service ();

    /** Returns the name of the service. */
    char const* serviceName () const;

    /** Notify a root service and its children to stop, and block until stopped.
        If the service was already notified, it is not notified again.
        The call blocks until the service and all of its children have stopped.

        Thread safety:
            Safe to call from any thread not associated with a Service.
            This function may only be called once.

        @param stream An optional Journal stream on which to log progress.
    */
    void serviceStop (Journal::Stream stream = Journal::Stream());

    /** Notify a root service and children to stop, without waiting.
        If the service was already notified, it is not notified again.
        While this is safe to call more than once, only the first call
        has any effect.
        
        Thread safety:
            Safe to call from any thread at any time.
    */
    void serviceStopAsync ();

    /** Returns `true` if the service should stop.
        Call from the derived class to determine if a long-running
        operation should be canceled.

        Note that this is not appropriate for either threads, or asynchronous
        I/O. For threads, use the thread-specific facilities available to
        inform the thread that it should exi. For asynchronous I/O, cancel
        all pending operations inside the onServiceStop overide.

        Thread safety:
            Safe to call from any thread at any time.

        @see onServiceStop
    */
    bool isServiceStopping ();
  
    /** Returns `true` if the service has stopped.

        Thread safety:
            Safe to call from any thread at any time.
    */
    bool isServiceStopped ();

    /** Returns `true` if all children have stopped.
        Children of services with no children are considered stopped if
        the service has been notified.

        Thread safety:
            Safe to call from any thread at any time.
    */
    bool areServiceChildrenStopped ();

    /** Called by derived classes to indicate that the service has stopped.
        The derived class must call this either after isServiceStopping
        returns `true`, or when onServiceStop is called, or else a call
        to serviceStop will never return.

        Thread safety:
            Safe to call from any thread at any time.
    */
    void serviceStopped ();

    /** Called when the stop notification is issued.
        
        The call is made on an unspecified, implementation-specific thread.
        onServiceStop and onServiceChildrenStopped will never be called
        concurrently, across all Service objects descended from the same root,
        inclusive of the root.

        It is safe to call isServiceStopping, isServiceStopped, and 
        areServiceChildrenStopped from within this function; The values
        returned will always be valid and never change during the callback.

        The default implementation simply calls serviceStopped(). This is
        applicable when the Service has a trivial stop operation (or no
        stop operation), and we are merely using the Service API to position
        it as a dependency of some parent service.

        Thread safety:
            May not block for long periods.
            Guaranteed only to be called once.
            Must be safe to call from any thread at any time.
    */
    virtual void onServiceStop ();

    /** Called when all children of a service have stopped.

        The call is made on an unspecified, implementation-specific thread.
        onServiceStop and onServiceChildrenStopped will never be called
        concurrently, across all Service objects descended from the same root,
        inclusive of the root.
        
        It is safe to call isServiceStopping, isServiceStopped, and 
        areServiceChildrenStopped from within this function; The values
        returned will always be valid and never change during the callback.

        Thread safety:
            May not block for long periods.
            Guaranteed only to be called once.
            Must be safe to call from any thread at any time.
    */
    virtual void onServiceChildrenStopped ();

private:
    struct Child;
    typedef LockFreeStack <Child> Children;

    struct Child : Children::Node
    {
        Child (Service* service_) : service (service_)
        {
        }

        Service* service;
    };

    void stopAsyncRecursive ();
    void stopRecursive (Journal::Stream stream);

    char const* m_name;
    bool m_root;
    Child m_child;
    Children m_children;

    // Flag that we called serviceStop. This is for diagnostics.
    bool m_calledServiceStop;

    // Atomic flag to make sure we only call serviceStopAsync once.
    Atomic <int> m_calledStopAsync;

    // Flag that this service stopped. Never goes back to false.
    bool volatile m_stopped;

    // Flag that all children have stopped (recursive). Never goes back to false.
    bool volatile m_childrenStopped;

    // serviceStop() blocks on this event until serviceStopped() is called.
    WaitableEvent m_stoppedEvent;
};

}

#endif
