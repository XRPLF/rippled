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

#ifndef RIPPLE_CORE_LOADMONITOR_H_INCLUDED
#define RIPPLE_CORE_LOADMONITOR_H_INCLUDED

#include <ripple/basics/UptimeClock.h>
#include <ripple/beast/utility/Journal.h>
#include <chrono>
#include <functional>
#include <mutex>
#include <optional>

namespace ripple {

using LoadSampler = std::function<void(
    char const*,
    std::chrono::steady_clock::duration,
    std::chrono::steady_clock::duration)>;

/** Monitors load levels and response times

    @todo Consider renaming this, since having both a LoadManager and a
          LoadMonitor is confusing, especially considering that they do
          different things.
 */
class LoadMonitor
{
public:
    struct Stats
    {
        std::uint64_t count = 0;

        std::chrono::milliseconds averageLatency{};
        std::chrono::milliseconds peakLatency{};

        bool isOverloaded = false;
    };

    explicit LoadMonitor(
        std::optional<std::chrono::milliseconds> targetAverageLatency,
        std::optional<std::chrono::milliseconds> targetPeakLatency,
        beast::Journal j);

    LoadMonitor(LoadMonitor const& other) = delete;
    LoadMonitor&
    operator=(LoadMonitor const& other) = delete;

    LoadMonitor(LoadMonitor&& other) = delete;
    LoadMonitor&
    operator=(LoadMonitor&& other) = delete;

    void
    addSamples(int count, std::chrono::milliseconds latency);

    Stats
    getStats();

    bool
    isOver();

    /** A reference to a function object used to report performance events. */
    std::reference_wrapper<LoadSampler const>
    sample()
    {
        return std::cref(eventCallback_);
    }

private:
    void
    update();

    std::mutex mutex_;

    std::uint64_t samples_ = 0;
    std::chrono::milliseconds averageLatency_{};
    std::chrono::milliseconds peakLatency_{};
    std::optional<std::chrono::milliseconds> targetAverageLatency_{};
    std::optional<std::chrono::milliseconds> targetPeakLatency_{};
    UptimeClock::time_point lastUpdate_;
    beast::Journal const j_;
    LoadSampler eventCallback_;
};

}  // namespace ripple

#endif
