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

void
Stoppable::onStart()
{
}

//------------------------------------------------------------------------------

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

//------------------------------------------------------------------------------

RootStoppable::RootStoppable(std::string name)
    : Stoppable(std::move(name), *this)
{
}

bool
RootStoppable::isStopping() const
{
    // TODO [C++20]: When `stopEntered_` is changed to a `std::atomic_flag`,
    // this implicit call to `load` needs to change to a call to `test`.
    return stopEntered_;
}

void
RootStoppable::start()
{
    if (startEntered_.exchange(true))
        return;
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

    stopAsyncRecursive(j);
}

}  // namespace ripple
