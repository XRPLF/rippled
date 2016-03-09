//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2012, Vinnie Falco <vinnie.falco@gmail.com>

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

#include <beast/threads/Stoppable.h>
#include <cassert>

namespace beast {

Stoppable::Stoppable (char const* name, RootStoppable& root)
    : m_name (name)
    , m_root (root)
    , m_child (this)
    , m_started (false)
    , m_stopped (false)
    , m_childrenStopped (false)
{
}

Stoppable::Stoppable (char const* name, Stoppable& parent)
    : m_name (name)
    , m_root (parent.m_root)
    , m_child (this)
    , m_started (false)
    , m_stopped (false)
    , m_childrenStopped (false)
{
    // Must not have stopping parent.
    assert (! parent.isStopping());

    parent.m_children.push_front (&m_child);
}

Stoppable::~Stoppable ()
{
    // Children must be stopped.
    assert (!m_started || m_childrenStopped);
}

bool Stoppable::isStopping() const
{
    return m_root.isStopping();
}

bool Stoppable::isStopped () const
{
    return m_stopped;
}

bool Stoppable::areChildrenStopped () const
{
    return m_childrenStopped;
}

void Stoppable::stopped ()
{
    m_stoppedEvent.signal();
}

void Stoppable::onPrepare ()
{
}

void Stoppable::onStart ()
{
}

void Stoppable::onStop ()
{
    stopped();
}

void Stoppable::onChildrenStopped ()
{
}

//------------------------------------------------------------------------------

void Stoppable::prepareRecursive ()
{
    for (Children::const_iterator iter (m_children.cbegin ());
        iter != m_children.cend(); ++iter)
        iter->stoppable->prepareRecursive ();
    onPrepare ();
}

void Stoppable::startRecursive ()
{
    onStart ();
    for (Children::const_iterator iter (m_children.cbegin ());
        iter != m_children.cend(); ++iter)
        iter->stoppable->startRecursive ();
}

void Stoppable::stopAsyncRecursive (Journal j)
{
    using namespace std::chrono;
    auto const start = high_resolution_clock::now();
    onStop ();
    auto const ms = duration_cast<milliseconds>(
        high_resolution_clock::now() - start).count();

#ifdef NDEBUG
    if (ms >= 10)
        if (auto stream = j.fatal())
            stream << m_name << "::onStop took " << ms << "ms";
#else
    (void)ms;
#endif

    for (Children::const_iterator iter (m_children.cbegin ());
        iter != m_children.cend(); ++iter)
        iter->stoppable->stopAsyncRecursive(j);
}

void Stoppable::stopRecursive (Journal j)
{
    // Block on each child from the bottom of the tree up.
    //
    for (Children::const_iterator iter (m_children.cbegin ());
        iter != m_children.cend(); ++iter)
        iter->stoppable->stopRecursive (j);

    // if we get here then all children have stopped
    //
    m_childrenStopped = true;
    onChildrenStopped ();

    // Now block on this Stoppable.
    //
    bool const timedOut (! m_stoppedEvent.wait (1 * 1000)); // milliseconds
    if (timedOut)
    {
        if (auto stream = j.error())
            stream << "Waiting for '" << m_name << "' to stop";
        m_stoppedEvent.wait ();
    }

    // once we get here, we know the stoppable has stopped.
    m_stopped = true;
}

//------------------------------------------------------------------------------

RootStoppable::RootStoppable (char const* name)
    : Stoppable (name, *this)
    , m_prepared (false)
    , m_calledStop (false)
    , m_calledStopAsync (false)
{
}

bool RootStoppable::isStopping() const
{
    return m_calledStopAsync;
}

void RootStoppable::prepare ()
{
    if (m_prepared.exchange (true) == false)
        prepareRecursive ();
}

void RootStoppable::start ()
{
    // Courtesy call to prepare.
    if (m_prepared.exchange (true) == false)
        prepareRecursive ();

    if (m_started.exchange (true) == false)
        startRecursive ();
}

void RootStoppable::stop (Journal j)
{
    // Must have a prior call to start()
    assert (m_started);

    {
        std::lock_guard<std::mutex> lock(m_);
        if (m_calledStop)
        {
            if (auto stream = j.warn())
                stream << "Stoppable::stop called again";
            return;
        }
        m_calledStop = true;
        c_.notify_all();
    }
    stopAsync (j);
    stopRecursive (j);
}

void RootStoppable::stopAsync(Journal j)
{
    if (m_calledStopAsync.exchange (true) == false)
        stopAsyncRecursive(j);
}

}
