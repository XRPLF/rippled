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
#include <chrono>
#include <cassert>

namespace beast {

Stoppable::Stoppable (std::string const& name, RootStoppable& root)
    : m_name (name)
    , m_root (root)
    , m_started (false)
    , m_stopped (false)
    , m_childrenStopped (false)
{
}

Stoppable::Stoppable (std::string const& name, Stoppable& parent)
    : m_name (name)
    , m_root (parent.m_root)
    , m_started (false)
    , m_stopped (false)
    , m_childrenStopped (false)
{
    // Must not have stopping parent.
    assert (! parent.isStopping());

    parent.addChild(this);
}

Stoppable::~Stoppable ()
{
    // Children must be stopped.
    assert (!m_started || m_childrenStopped);
}

void Stoppable::addChild(Stoppable* child) {
    std::lock_guard<std::mutex> lock(m_childrenMutex);
    m_children.push_front(child);
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
    std::lock_guard<std::mutex> lock(m_childrenMutex);
    m_stopped = true;
    m_stoppedEvent.notify_one();
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

void Stoppable::prepareRecursive (Journal& journal)
{
    journal.debug << "Stoppable::prepareRecursive called for: " << m_name;
    {
        std::lock_guard<std::mutex> lock(m_childrenMutex);
        for (auto const& child : m_children) 
            child->prepareRecursive(journal);
    }
    onPrepare ();
}

void Stoppable::startRecursive (Journal& journal)
{
    journal.debug << "Stoppable::startRecursive called for: " << m_name;
    onStart ();
    std::lock_guard<std::mutex> lock(m_childrenMutex);
    for (auto const& child: m_children)
        child->startRecursive(journal);
}

void Stoppable::stopAsyncRecursive (Journal& journal)
{
    journal.debug << "Stoppable::stopAsyncRecursive called for: " << m_name;
    onStop ();
    std::lock_guard<std::mutex> lock(m_childrenMutex);
    for (auto const& child: m_children)
        child->stopAsyncRecursive(journal);
}

void Stoppable::stopRecursive (Journal& journal)
{
    journal.debug << "Stoppable::stopRecursive called for: " << m_name;

    // Block on each child from the bottom of the tree up.
    //
    {
        std::lock_guard<std::mutex> lock(m_childrenMutex);
        for (auto const& child: m_children)
            child->stopRecursive(journal);
    }

    // if we get here then all children have stopped
    //
    m_childrenStopped = true;
    onChildrenStopped ();

    bool hasStopped = false;
    // Now block on this Stoppable for 1 second.
    //
    {
        std::unique_lock<std::mutex> lock(m_childrenMutex);
        hasStopped = m_stoppedEvent.wait_for(lock, 
            std::chrono::seconds(1), [&] {
            return isStopped();
        });
    }

    // Release lock temporarily in case stopped() is blocking.
    //
    if (!hasStopped)
    {
        std::unique_lock<std::mutex> lock(m_childrenMutex);
        journal.warning << "Waiting for '" << m_name << "' to stop";
        m_stoppedEvent.wait(lock,[&]{ return isStopped(); });
    }
    journal.info << "'" << m_name << "' has stopped";
}

//------------------------------------------------------------------------------

RootStoppable::RootStoppable (std::string const& name)
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

void RootStoppable::prepare (Journal journal)
{
    if (m_prepared.exchange (true) == false)
        prepareRecursive (journal);
}

void RootStoppable::start (Journal journal)
{
    // Courtesy call to prepare.
    if (m_prepared.exchange (true) == false)
        prepareRecursive (journal);

    if (m_started.exchange (true) == false)
        startRecursive (journal);
}

void RootStoppable::stop (Journal journal)
{
    // Must have a prior call to start()
    assert (m_started);

    if (m_calledStop.exchange (true) == true)
    {
        journal.warning << "Stoppable::stop called again";
        return;
    }

    stopAsync (journal);
    stopRecursive (journal);
}

void RootStoppable::stopAsync (Journal journal)
{
    if (m_calledStopAsync.exchange (true) == false)
        stopAsyncRecursive (journal);
}
}
