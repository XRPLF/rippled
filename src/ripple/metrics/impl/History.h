//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

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

#ifndef METRICS_HISTORY_H_INCLUDED
#define METRICS_HISTORY_H_INCLUDED

#include <chrono>
#include <deque>
#include <vector>
#include <array>

namespace ripple {
namespace metrics {
namespace impl {

using clock_type = std::chrono::steady_clock;

struct resolution
{
  clock_type::duration duration;
};

std::array <resolution, 4> constexpr resolutions {{
  { std::chrono::seconds(1) },
  { std::chrono::minutes(1) },
  { std::chrono::hours(1) },
  { std::chrono::duration<int, std::ratio<86400> >(1) }
}};

struct bucket
{
    bucket()
        : count{0}, min{0}, max{0}, avg{0}
    {
    }

    std::uint64_t count; // of samples in the bucket
    std::uint64_t min;
    std::uint64_t max;
    std::uint64_t avg;
};

struct history
{
    clock_type::time_point start; // of the first bucket
    resolution res;

    // Aggregate data covering the period:
    // [start, start+buckets.size()*duration)
    // where duration is 1 second, 1 minute, 1 hour, or 1 day.
    std::deque <bucket> buckets;
};

struct histories
{
    std::array <history, resolutions.size()> data;
    std::vector <std::uint64_t> samples;

    histories() {
      clock_type::time_point now = clock_type::now();
      for(auto i = data.begin();i != data.end(); i++) {
        i->start = now;
      }
    }
};

void addValue(histories& hist, const std::uint64_t v);

void aggregateSamples(histories& h, const clock_type::time_point& now);

} // namespace impl
} // namespace metrics
} // namespace ripple

#endif // METRICS_HISTORY_H_INCLUDED
