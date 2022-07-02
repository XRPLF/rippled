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

#ifndef RIPPLE_CORE_LOADEVENT_H_INCLUDED
#define RIPPLE_CORE_LOADEVENT_H_INCLUDED

#include <ripple/core/LoadMonitor.h>
#include <chrono>
#include <string>

namespace ripple {

// VFALCO TODO Rename LoadEvent to ScopedLoadSample
class LoadEvent
{
public:
    LoadEvent(
        std::reference_wrapper<LoadSampler const> callback,
        std::string name,
        bool shouldStart);

    LoadEvent(LoadEvent&& other);
    LoadEvent&
    operator=(LoadEvent&& other);

    LoadEvent&
    operator=(LoadEvent const&) = delete;
    LoadEvent(LoadEvent const&) = delete;

    ~LoadEvent();

    std::string const&
    name() const;

    // Start the measurement. If already started, then
    // restart, assigning the elapsed time to the "waiting"
    // state.
    void
    start();

    // Stop the measurement and report the results. The
    // time reported is measured from the last call to
    // start.
    void
    stop();

private:
    // The name for this event.
    std::string name_;

    // The callback to invoke when we stop. This will only
    // be invoked if `neutered_` is `false`.
    std::reference_wrapper<LoadSampler const> callback_;

    // Represents the time we last transitioned states
    std::chrono::steady_clock::time_point mark_;

    // The time we spent waiting and running respectively
    std::chrono::steady_clock::duration timeWaiting_;
    std::chrono::steady_clock::duration timeRunning_;

    // Represents our current state
    bool running_;

    // Determines whether the callback should be invoked
    bool neutered_;
};

}  // namespace ripple

#endif
