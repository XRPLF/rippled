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

#include <chrono>
#include <memory>
#include <string>

namespace ripple {

class LoadMonitor;

// VFALCO NOTE What is the difference between a LoadEvent and a LoadMonitor?
// VFALCO TODO Rename LoadEvent to ScopedLoadSample
//
//        This looks like a scoped elapsed time measuring class
//
class LoadEvent
{
public:
    // VFALCO TODO remove the dependency on LoadMonitor. Is that possible?
    LoadEvent (LoadMonitor& monitor,
               std::string const& name,
               bool shouldStart);
    LoadEvent(LoadEvent const&) = delete;

    ~LoadEvent ();

    std::string const&
    name () const;

    // The time spent waiting.
    std::chrono::steady_clock::duration
    waitTime() const;

    // The time spent running.
    std::chrono::steady_clock::duration
    runTime() const;

    void setName (std::string const& name);

    // Start the measurement. If already started, then
    // restart, assigning the elapsed time to the "waiting"
    // state.
    void start ();

    // Stop the measurement and report the results. The
    // time reported is measured from the last call to
    // start.
    void stop ();

private:
    LoadMonitor& monitor_;

    // Represents our current state
    bool running_;

    // The name associated with this event, if any.
    std::string name_;

    // Represents the time we last transitioned states
    std::chrono::steady_clock::time_point mark_;

    // The time we spent waiting and running respectively
    std::chrono::steady_clock::duration timeWaiting_;
    std::chrono::steady_clock::duration timeRunning_;
};

} // ripple

#endif
