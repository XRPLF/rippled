//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

namespace ripple
{

Service::Service (char const* name)
    : m_name (name)
    , m_root (true)
    , m_child (this)
    , m_calledServiceStop (false)
    , m_stopped (false)
    , m_childrenStopped (false)
{
}

Service::Service (char const* name, Service* parent)
    : m_name (name)
    , m_root (parent != nullptr)
    , m_child (this)
    , m_calledServiceStop (false)
    , m_stopped (false)
    , m_childrenStopped (false)
{
    if (parent != nullptr)
    {
        // must not have had stop called
        bassert (! parent->isServiceStopping());

        parent->m_children.push_front (&m_child);
    }
}

Service::Service (char const* name, Service& parent)
    : m_name (name)
    , m_root (false)
    , m_child (this)
    , m_calledServiceStop (false)
    , m_stopped (false)
    , m_childrenStopped (false)
{
    // must not have had stop called
    bassert (! parent.isServiceStopping());

    parent.m_children.push_front (&m_child);
}

Service::~Service ()
{
    // must be stopped
    bassert (m_stopped);

    // children must be stopped
    bassert (m_childrenStopped);
}

char const* Service::serviceName () const
{
    return m_name;
}

void Service::serviceStop (Journal::Stream stream)
{
    // may only be called once
    if (m_calledServiceStop)
        return;

    m_calledServiceStop = true;

    // must be called from a root service
    bassert (m_root);

    // send the notification
    serviceStopAsync ();

    // now block on the tree of Service objects from the leaves up.
    stopRecursive (stream);
}

void Service::serviceStopAsync ()
{
    // must be called from a root service
    bassert (m_root);

    stopAsyncRecursive ();
}

bool Service::isServiceStopping ()
{
    return m_calledStopAsync.get() != 0;
}

bool Service::isServiceStopped ()
{
    return m_stopped;
}

bool Service::areServiceChildrenStopped ()
{
    return m_childrenStopped;
}

void Service::serviceStopped ()
{
    m_stoppedEvent.signal();
}

void Service::onServiceStop()
{
    serviceStopped();
}

void Service::onServiceChildrenStopped ()
{
}

//------------------------------------------------------------------------------

void Service::stopAsyncRecursive ()
{
    // make sure we only do this once
    if (m_root)
    {
        // if this fails, some other thread got to it first
        if (! m_calledStopAsync.compareAndSetBool (1, 0))
            return;
    }
    else
    {
        // can't possibly already be set
        bassert (m_calledStopAsync.get() == 0);

        m_calledStopAsync.set (1);
    }

    // notify this service
    onServiceStop ();

    // notify children
    for (Children::const_iterator iter (m_children.cbegin ());
        iter != m_children.cend(); ++iter)
    {
        iter->service->stopAsyncRecursive();
    }
}

void Service::stopRecursive (Journal::Stream stream)
{
    // Block on each child recursively. Thinking of the Service
    // hierarchy as a tree with the root at the top, we will block
    // first on leaves, and then at each successivly higher level.
    //
    for (Children::const_iterator iter (m_children.cbegin ());
        iter != m_children.cend(); ++iter)
    {
        iter->service->stopRecursive (stream);
    }

    // Once we get here, we either have no children, or all of
    // our children have stopped, so update state accordingly.
    //
    m_childrenStopped = true;

    // Notify derived class that children have stopped.
    onServiceChildrenStopped ();

    // Block until this service stops. First we do a timed wait of 1 second, and
    // if that times out we report to the Journal and then do an infinite wait.
    //
    bool const timedOut (! m_stoppedEvent.wait (1 * 1000)); // milliseconds
    if (timedOut)
    {
        stream << "Service: Waiting for '" << serviceName() << "' to stop";
        m_stoppedEvent.wait ();
    }

    // once we get here, we know the service has stopped.
    m_stopped = true;
}

}
