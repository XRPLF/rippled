//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <ripple/basics/contract.h>
#include <ripple/core/Stoppable.h>

#include <cassert>

namespace ripple {

Stoppable::Stoppable(std::string name, RootStoppable& root)
    : m_name(std::move(name)), m_root(root), m_child(this)
{
}

Stoppable::Stoppable(std::string name, Stoppable& parent)
    : m_name(std::move(name)), m_root(parent.m_root), m_child(this)
{
    setParent(parent);
}

Stoppable::~Stoppable()
{
}

void
Stoppable::setParent(Stoppable& parent)
{
    assert(!hasParent_);
    assert(!parent.isStopping());
    assert(std::addressof(m_root) == std::addressof(parent.m_root));

    parent.m_children.push_front(&m_child);
    hasParent_ = true;
}

bool
Stoppable::isStopping() const
{
    return m_root.isStopping();
}

bool
Stoppable::isStopped() const
{
    return m_stopped;
}

bool
Stoppable::areChildrenStopped() const
{
    return m_childrenStopped;
}

void
Stoppable::stopped()
{
    std::lock_guard lk{m_mut};
    m_is_stopping = true;
    m_cv.notify_all();
}

void
Stoppable::onPrepare()
{
}

void
Stoppable::onStart()
{
}

void
Stoppable::onStop()
{
    stopped();
}

void
Stoppable::onChildrenStopped()
{
}

//------------------------------------------------------------------------------

void
Stoppable::prepareRecursive()
{
    for (Children::const_iterator iter(m_children.cbegin());
         iter != m_children.cend();
         ++iter)
        iter->stoppable->prepareRecursive();
    onPrepare();
}

void
Stoppable::startRecursive()
{
    onStart();
    for (Children::const_iterator iter(m_children.cbegin());
         iter != m_children.cend();
         ++iter)
        iter->stoppable->startRecursive();
}

void
Stoppable::stopAsyncRecursive(beast::Journal j)
{
    onStop();

    for (Children::const_iterator iter(m_children.cbegin());
         iter != m_children.cend();
         ++iter)
        iter->stoppable->stopAsyncRecursive(j);
}

void
Stoppable::stopRecursive(beast::Journal j)
{
    // Block on each child from the bottom of the tree up.
    //
    for (Children::const_iterator iter(m_children.cbegin());
         iter != m_children.cend();
         ++iter)
        iter->stoppable->stopRecursive(j);

    // if we get here then all children have stopped
    //
    m_childrenStopped = true;
    onChildrenStopped();

    // Now block on this Stoppable until m_is_stopping is set by stopped().
    //
    using namespace std::chrono_literals;
    std::unique_lock<std::mutex> lk{m_mut};
    if (!m_cv.wait_for(lk, 1s, [this] { return m_is_stopping; }))
    {
        if (auto stream = j.error())
            stream << "Waiting for '" << m_name << "' to stop";
        m_cv.wait(lk, [this] { return m_is_stopping; });
    }
    m_stopped = true;
}

//------------------------------------------------------------------------------

RootStoppable::RootStoppable(std::string name)
    : Stoppable(std::move(name), *this)
{
}

RootStoppable::~RootStoppable()
{
    using namespace std::chrono_literals;
    jobCounter_.join(m_name.c_str(), 1s, debugLog());
}

bool
RootStoppable::isStopping() const
{
    return stopEntered_;
}

void
RootStoppable::start()
{
    if (startEntered_.exchange(true))
        return;
    prepareRecursive();
    startRecursive();
    startExited_ = true;
}

void
RootStoppable::stop(beast::Journal j)
{
    // Must have a prior call to start()
    assert(startExited_);

    bool alreadyCalled;
    {
        // Even though stopEntered_ is atomic, we change its value under a
        // lock.  This removes a small timing window that occurs if the
        // waiting thread is handling a spurious wakeup while stopEntered_
        // changes state.
        std::unique_lock<std::mutex> lock(m_);
        alreadyCalled = stopEntered_.exchange(true);
    }
    if (alreadyCalled)
    {
        if (auto stream = j.warn())
            stream << "RootStoppable::stop called again";
        return;
    }

    // Wait until all in-flight JobQueue Jobs are completed.
    using namespace std::chrono_literals;
    jobCounter_.join(m_name.c_str(), 1s, j);

    c_.notify_all();
    stopAsyncRecursive(j);
    stopRecursive(j);
}

}  // namespace ripple
