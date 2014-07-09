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

#ifndef RIPPLE_LOADMONITOR_H_INCLUDED
#define RIPPLE_LOADMONITOR_H_INCLUDED

#include <ripple/module/core/functional/LoadEvent.h>

namespace ripple {

// Monitors load levels and response times

// VFALCO TODO Rename this. Having both LoadManager and LoadMonitor is confusing.
//
class LoadMonitor
{
public:
    LoadMonitor ();

    void addCount ();

    void addLatency (int latency);

    void addLoadSample (LoadEvent const& sample);

    void addSamples (int count, std::chrono::milliseconds latency);

    void setTargetLatency (uint64 avg, uint64 pk);

    bool isOverTarget (uint64 avg, uint64 peak);

    // VFALCO TODO make this return the values in a struct.
    struct Stats
    {
        Stats();

        uint64 count;
        uint64 latencyAvg;
        uint64 latencyPeak;
        bool isOverloaded;
    };

    Stats getStats ();

    bool isOver ();

private:
    static std::string printElapsed (double seconds);

    void update ();

    typedef RippleMutex LockType;
    typedef std::lock_guard <LockType> ScopedLockType;
    LockType mLock;

    uint64 mCounts;
    int           mLatencyEvents;
    uint64 mLatencyMSAvg;
    uint64 mLatencyMSPeak;
    uint64 mTargetLatencyAvg;
    uint64 mTargetLatencyPk;
    int           mLastUpdate;
};

} // ripple

#endif
