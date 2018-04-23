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

#include <ripple/basics/Log.h>
#include <ripple/basics/UptimeTimer.h>
#include <ripple/basics/date.h>
#include <ripple/core/LoadMonitor.h>

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

LoadMonitor::LoadMonitor (beast::Journal j)
    : mCounts (0)
    , mLatencyEvents (0)
    , mLatencyMSAvg (0)
    , mLatencyMSPeak (0)
    , mTargetLatencyAvg (0)
    , mTargetLatencyPk (0)
    , mLastUpdate (UptimeClock::now())
    , j_ (j)
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
    using namespace std::chrono_literals;
    auto now = UptimeClock::now();
    if (now == mLastUpdate) // current
        return;

    // VFALCO TODO Why 8?
    if ((now < mLastUpdate) || (now > (mLastUpdate + 8s)))
    {
        // way out of date
        mCounts = 0;
        mLatencyEvents = 0;
        mLatencyMSAvg = 0ms;
        mLatencyMSPeak = 0ms;
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
        mLastUpdate += 1s;
        mCounts -= ((mCounts + 3) / 4);
        mLatencyEvents -= ((mLatencyEvents + 3) / 4);
        mLatencyMSAvg -= (mLatencyMSAvg / 4);
        mLatencyMSPeak -= (mLatencyMSPeak / 4);
    }
    while (mLastUpdate < now);
}

void LoadMonitor::addLoadSample (LoadEvent const& s)
{
    using namespace std::chrono;

    auto const total = s.runTime() + s.waitTime();
    // Don't include "jitter" as part of the latency
    auto const latency = total < 2ms ? 0ms : date::round<milliseconds>(total);

    if (latency > 500ms)
    {
        auto mj = (latency > 1s) ? j_.warn() : j_.info();
        JLOG (mj) << "Job: " << s.name() <<
            " run: " << date::round<milliseconds>(s.runTime()).count() <<
            "ms" << " wait: " <<
            date::round<milliseconds>(s.waitTime()).count() << "ms";
    }

    addSamples (1, latency);
}

/* Add multiple samples
   @param count The number of samples to add
   @param latencyMS The total number of milliseconds
*/
void LoadMonitor::addSamples (int count, std::chrono::milliseconds latency)
{
    std::lock_guard<std::mutex> sl (mutex_);

    update ();
    mCounts += count;
    mLatencyEvents += count;
    mLatencyMSAvg += latency;
    mLatencyMSPeak += latency;

    auto const latencyPeak = mLatencyEvents * latency * 4 / count;

    if (mLatencyMSPeak < latencyPeak)
        mLatencyMSPeak = latencyPeak;
}

void LoadMonitor::setTargetLatency (std::chrono::milliseconds avg,
                                    std::chrono::milliseconds pk)
{
    mTargetLatencyAvg  = avg;
    mTargetLatencyPk = pk;
}

bool LoadMonitor::isOverTarget (std::chrono::milliseconds avg,
                                std::chrono::milliseconds peak)
{
    using namespace std::chrono_literals;
    return (mTargetLatencyPk > 0ms && (peak > mTargetLatencyPk)) ||
           (mTargetLatencyAvg > 0ms && (avg > mTargetLatencyAvg));
}

bool LoadMonitor::isOver ()
{
    std::lock_guard<std::mutex> sl (mutex_);

    update ();

    if (mLatencyEvents == 0)
        return 0;

    return isOverTarget (mLatencyMSAvg / (mLatencyEvents * 4), mLatencyMSPeak / (mLatencyEvents * 4));
}

LoadMonitor::Stats LoadMonitor::getStats ()
{
    using namespace std::chrono_literals;
    Stats stats;

    std::lock_guard<std::mutex> sl (mutex_);

    update ();

    stats.count = mCounts / 4;

    if (mLatencyEvents == 0)
    {
        stats.latencyAvg = 0ms;
        stats.latencyPeak = 0ms;
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
