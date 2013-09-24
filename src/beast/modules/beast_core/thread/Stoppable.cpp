//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

Stoppable::Stoppable (char const* name, Stoppable& parent)
    : m_name (name)
    , m_root (false)
    , m_child (this)
    , m_calledStop (false)
    , m_stopped (false)
    , m_childrenStopped (false)
{
    // must not have had stop called
    bassert (! parent.isStopping());

    parent.m_children.push_front (&m_child);
}

Stoppable::Stoppable (char const* name, Stoppable* parent)
    : m_name (name)
    , m_root (parent == nullptr)
    , m_child (this)
    , m_calledStop (false)
    , m_stopped (false)
    , m_childrenStopped (false)
{
    if (parent != nullptr)
    {
        // must not have had stop called
        bassert (! parent->isStopping());

        parent->m_children.push_front (&m_child);
    }
}

Stoppable::~Stoppable ()
{
    // must be stopped
    bassert (m_stopped);

    // children must be stopped
    bassert (m_childrenStopped);
}

void Stoppable::stop (Journal::Stream stream)
{
    // may only be called once
    if (m_calledStop)
        return;

    m_calledStop = true;

    // must be called from a root stoppable
    bassert (m_root);

    // send the notification
    stopAsync ();

    // now block on the tree of Stoppable objects from the leaves up.
    stopRecursive (stream);
}

void Stoppable::stopAsync ()
{
    // must be called from a root stoppable
    bassert (m_root);

    stopAsyncRecursive ();
}

bool Stoppable::isStopping ()
{
    return m_calledStopAsync.get() != 0;
}

bool Stoppable::isStopped ()
{
    return m_stopped;
}

bool Stoppable::areChildrenStopped ()
{
    return m_childrenStopped;
}

void Stoppable::stopped ()
{
    m_stoppedEvent.signal();
}

void Stoppable::onStop()
{
    stopped();
}

void Stoppable::onChildrenStopped ()
{
}

//------------------------------------------------------------------------------

void Stoppable::stopAsyncRecursive ()
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

    // notify this stoppable
    onStop ();

    // notify children
    for (Children::const_iterator iter (m_children.cbegin ());
        iter != m_children.cend(); ++iter)
    {
        iter->stoppable->stopAsyncRecursive();
    }
}

void Stoppable::stopRecursive (Journal::Stream stream)
{
    // Block on each child recursively. Thinking of the Stoppable
    // hierarchy as a tree with the root at the top, we will block
    // first on leaves, and then at each successivly higher level.
    //
    for (Children::const_iterator iter (m_children.cbegin ());
        iter != m_children.cend(); ++iter)
    {
        iter->stoppable->stopRecursive (stream);
    }

    // Once we get here, we either have no children, or all of
    // our children have stopped, so update state accordingly.
    //
    m_childrenStopped = true;

    // Notify derived class that children have stopped.
    onChildrenStopped ();

    // Block until this stoppable stops. First we do a timed wait of 1 second, and
    // if that times out we report to the Journal and then do an infinite wait.
    //
    bool const timedOut (! m_stoppedEvent.wait (1 * 1000)); // milliseconds
    if (timedOut)
    {
        stream << "Waiting for '" << m_name << "' to stop";
        m_stoppedEvent.wait ();
    }

    // once we get here, we know the stoppable has stopped.
    m_stopped = true;
}
