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
#include <ripple/basics/UptimeClock.h>
#include <ripple/core/LoadMonitor.h>

namespace ripple {

LoadMonitor::LoadMonitor(
    std::optional<std::chrono::milliseconds> targetAverageLatency,
    std::optional<std::chrono::milliseconds> targetPeakLatency,
    beast::Journal j)
    : targetAverageLatency_(targetAverageLatency)
    , targetPeakLatency_(targetPeakLatency)
    , lastUpdate_(UptimeClock::now())
    , j_(j)
    , eventCallback_([this](
                         char const* name,
                         std::chrono::steady_clock::duration run,
                         std::chrono::steady_clock::duration wait) {
        using namespace std::chrono;

        auto const latency = [total = run + wait] {
            // Don't include "jitter" as part of the latency
            if (total < 2ms)
                return 0ms;

            return round<milliseconds>(total);
        }();

        if (latency > 500ms)
        {
            auto mj = (latency > 1s) ? j_.warn() : j_.info();
            JLOG(mj) << "Job: " << name
                     << " run: " << round<milliseconds>(run).count() << "ms"
                     << " wait: " << round<milliseconds>(wait).count() << "ms";
        }

        addSamples(1, latency);
    })
{
}

LoadMonitor::LoadMonitor(LoadMonitor&& other) : j_(other.j_)
{
    std::lock_guard lock(other.mutex_);

    samples_ = other.samples_;
    averageLatency_ = other.averageLatency_;
    peakLatency_ = other.peakLatency_;
    targetAverageLatency_ = other.targetAverageLatency_;
    targetPeakLatency_ = other.targetPeakLatency_;
    lastUpdate_ = other.lastUpdate_;

    std::swap(eventCallback_, other.eventCallback_);
}

void
LoadMonitor::update()
{
    using namespace std::chrono_literals;

    if (auto const now = UptimeClock::now(); now != lastUpdate_)
    {
        // VFALCO TODO Why 8?
        if ((now < lastUpdate_) || (now > (lastUpdate_ + 8s)))
        {
            // way out of date
            samples_ = 0;
            averageLatency_ = 0ms;
            peakLatency_ = 0ms;
            lastUpdate_ = now;
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
            lastUpdate_ += 1s;
            samples_ -= ((samples_ + 3) / 4);
            averageLatency_ -= (averageLatency_ / 4);
            peakLatency_ -= (peakLatency_ / 4);
        } while (lastUpdate_ < now);
    }
}

/* Add multiple samples
   @param count The number of samples to add
   @param latency The total number of milliseconds
*/
void
LoadMonitor::addSamples(int count, std::chrono::milliseconds latency)
{
    std::lock_guard sl(mutex_);

    update();
    samples_ += count;
    averageLatency_ += latency;
    peakLatency_ += latency;

    auto const latencyPeak = samples_ * latency * 4 / count;

    if (peakLatency_ < latencyPeak)
        peakLatency_ = latencyPeak;
}

bool
LoadMonitor::isOver()
{
    return getStats().isOverloaded;
}

LoadMonitor::Stats
LoadMonitor::getStats()
{
    using namespace std::chrono_literals;

    std::lock_guard sl(mutex_);

    update();

    Stats s;
    s.count = samples_ / 4;

    if (samples_ != 0)
    {
        s.averageLatency = averageLatency_ / (samples_ * 4);
        s.peakLatency = peakLatency_ / (samples_ * 4);
    }

    s.isOverloaded =
        (targetAverageLatency_.value_or(s.averageLatency) > s.averageLatency) ||
        (targetPeakLatency_.value_or(s.peakLatency) > s.peakLatency);

    return s;
}

}  // namespace ripple
