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

#include <ripple/core/LoadEvent.h>
#include <cassert>

namespace ripple {

LoadEvent::LoadEvent(
    std::reference_wrapper<LoadSampler const> callback,
    std::string name,
    bool shouldStart)
    : name_(std::move(name))
    , callback_(callback)
    , mark_(std::chrono::steady_clock::now())
    , timeWaiting_{}
    , timeRunning_{}
    , running_(shouldStart)
    , neutered_(false)
{
}

LoadEvent::LoadEvent(LoadEvent&& other)
    : name_(std::move(other.name_))
    , callback_(other.callback_)
    , mark_(other.mark_)
    , timeWaiting_(other.timeWaiting_)
    , timeRunning_(other.timeRunning_)
    , running_(other.running_)
    , neutered_(false)
{
    other.running_ = false;
    other.neutered_ = true;
}

LoadEvent&
LoadEvent::operator=(LoadEvent&& other)
{
    if (this != &other)
    {
        name_ = std::move(other.name_);
        callback_ = other.callback_;
        running_ = other.running_;
        mark_ = other.mark_;
        timeWaiting_ = other.timeWaiting_;
        timeRunning_ = other.timeRunning_;

        // Leave the moved-from object in a sane but "neutered" state.
        other.running_ = false;
        other.neutered_ = true;
    }

    return *this;
}

LoadEvent::~LoadEvent()
{
    if (running_)
        stop();
}

std::string const&
LoadEvent::name() const
{
    return name_;
}

void
LoadEvent::start()
{
    assert(!neutered_);

    auto const now = std::chrono::steady_clock::now();

    // If we had already called start, this call will
    // replace the previous one. Any time accumulated will
    // be counted as "waiting".
    timeWaiting_ += now - mark_;
    mark_ = now;
    running_ = true;
}

void
LoadEvent::stop()
{
    assert(!neutered_);
    assert(running_);

    auto const now = std::chrono::steady_clock::now();

    timeRunning_ += now - mark_;
    mark_ = now;
    running_ = false;

    if (!neutered_)
        callback_(name_.c_str(), timeRunning_, timeWaiting_);
}

}  // namespace ripple
