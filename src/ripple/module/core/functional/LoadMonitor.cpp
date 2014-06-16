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

#include <ripple/basics/utility/UptimeTimer.h>

namespace ripple {

/*

TODO
----

- Use Journal for logging

*/

//------------------------------------------------------------------------------

LoadMonitor::Stats::Stats()
    : count (0)
    , latencyAvg (0)
    , latencyPeak (0)
    , isOverloaded (false)
{
}

//------------------------------------------------------------------------------

SETUP_LOG (LoadMonitor)

LoadMonitor::LoadMonitor ()
    : mCounts (0)
    , mLatencyEvents (0)
    , mLatencyMSAvg (0)
    , mLatencyMSPeak (0)
    , mTargetLatencyAvg (0)
    , mTargetLatencyPk (0)
    , mLastUpdate (UptimeTimer::getInstance ().getElapsedSeconds ())
{
}

// VFALCO NOTE WHY do we need "the mutex?" This dependence on
//         a hidden global, especially a synchronization primitive,
//         is a flawed design.
//         It's not clear exactly which data needs to be protected.
//
// call with the mutex
void LoadMonitor::update ()
{
    int now = UptimeTimer::getInstance ().getElapsedSeconds ();

    // VFALCO TODO stop returning from the middle of functions.

    if (now == mLastUpdate) // current
        return;

    // VFALCO TODO Why 8?
    if ((now < mLastUpdate) || (now > (mLastUpdate + 8)))
    {
        // way out of date
        mCounts = 0;
        mLatencyEvents = 0;
        mLatencyMSAvg = 0;
        mLatencyMSPeak = 0;
        mLastUpdate = now;
        // VFALCO TODO don't return from the middle...
        return;
    }

    // do exponential decay
    /*
        David:
        
        "Imagine if you add 10 to something every second. And you
         also reduce it by 1/4 every second. It will "idle" at 40,
         correponding to 10 counts per second."
    */
    do
    {
        ++mLastUpdate;
        mCounts -= ((mCounts + 3) / 4);
        mLatencyEvents -= ((mLatencyEvents + 3) / 4);
        mLatencyMSAvg -= (mLatencyMSAvg / 4);
        mLatencyMSPeak -= (mLatencyMSPeak / 4);
    }
    while (mLastUpdate < now);
}

void LoadMonitor::addCount ()
{
    ScopedLockType sl (mLock);

    update ();
    ++mCounts;
}

void LoadMonitor::addLatency (int latency)
{
    // VFALCO NOTE Why does 1 become 0?
    if (latency == 1)
        latency = 0;

    ScopedLockType sl (mLock);

    update ();

    ++mLatencyEvents;
    mLatencyMSAvg += latency;
    mLatencyMSPeak += latency;

    // Units are quarters of a millisecond
    int const latencyPeak = mLatencyEvents * latency * 4;

    if (mLatencyMSPeak < latencyPeak)
        mLatencyMSPeak = latencyPeak;
}

std::string LoadMonitor::printElapsed (double seconds)
{
    std::stringstream ss;
    ss << (std::size_t (seconds * 1000 + 0.5)) << " ms";
    return ss.str();
}

void LoadMonitor::addLoadSample (LoadEvent const& sample)
{
    std::string const& name (sample.name());
    beast::RelativeTime const latency (sample.getSecondsTotal());

    if (latency.inSeconds() > 0.5)
    {
        WriteLog ((latency.inSeconds() > 1.0) ? lsWARNING : lsINFO, LoadMonitor)
            << "Job: " << name << " ExecutionTime: " << printElapsed (sample.getSecondsRunning()) <<
            " WaitingTime: " << printElapsed (sample.getSecondsWaiting());
    }

    // VFALCO NOTE Why does 1 become 0?
    std::size_t latencyMilliseconds (latency.inMilliseconds());
    if (latencyMilliseconds == 1)
        latencyMilliseconds = 0;

    ScopedLockType sl (mLock);

    update ();
    ++mCounts;
    ++mLatencyEvents;
    mLatencyMSAvg += latencyMilliseconds;
    mLatencyMSPeak += latencyMilliseconds;

    // VFALCO NOTE Why are we multiplying by 4?
    int const latencyPeak = mLatencyEvents * latencyMilliseconds * 4;

    if (mLatencyMSPeak < latencyPeak)
        mLatencyMSPeak = latencyPeak;
}

/* Add multiple samples
   @param count The number of samples to add
   @param latencyMS The total number of milliseconds
*/
void LoadMonitor::addSamples (int count, std::chrono::milliseconds latency)
{
    ScopedLockType sl (mLock);

    update ();
    mCounts += count;
    mLatencyEvents += count;
    mLatencyMSAvg += latency.count();
    mLatencyMSPeak += latency.count();

    int const latencyPeak = mLatencyEvents * latency.count() * 4 / count;

    if (mLatencyMSPeak < latencyPeak)
        mLatencyMSPeak = latencyPeak;
}

void LoadMonitor::setTargetLatency (std::uint64_t avg, std::uint64_t pk)
{
    mTargetLatencyAvg  = avg;
    mTargetLatencyPk = pk;
}

bool LoadMonitor::isOverTarget (std::uint64_t avg, std::uint64_t peak)
{
    return (mTargetLatencyPk && (peak > mTargetLatencyPk)) ||
           (mTargetLatencyAvg && (avg > mTargetLatencyAvg));
}

bool LoadMonitor::isOver ()
{
    ScopedLockType sl (mLock);

    update ();

    if (mLatencyEvents == 0)
        return 0;

    return isOverTarget (mLatencyMSAvg / (mLatencyEvents * 4), mLatencyMSPeak / (mLatencyEvents * 4));
}

LoadMonitor::Stats LoadMonitor::getStats ()
{
    Stats stats;

    ScopedLockType sl (mLock);

    update ();

    stats.count = mCounts / 4;

    if (mLatencyEvents == 0)
    {
        stats.latencyAvg = 0;
        stats.latencyPeak = 0;
    }
    else
    {
        stats.latencyAvg = mLatencyMSAvg / (mLatencyEvents * 4);
        stats.latencyPeak = mLatencyMSPeak / (mLatencyEvents * 4);
    }

    stats.isOverloaded = isOverTarget (stats.latencyAvg, stats.latencyPeak);

    return stats;
}

} // ripple
