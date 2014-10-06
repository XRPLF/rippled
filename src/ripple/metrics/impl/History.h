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

#include <ripple/common/seconds_clock.h>

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
  { ripple::days(1) }
}};

/**
 * Bucket: a bucket that holds aggregations of previous data
 *
 * Each History object contains a list of buckets. Each bucket represents an
 * aggregation of data points from buckets with finer resolution.
 */
struct Bucket
{
    std::uint64_t count; // of samples in the Bucket
    std::uint64_t min;
    std::uint64_t max;
    std::uint64_t avg;
};

struct History
{
    clock_type::time_point start; // of the first Bucket
    resolution res;

    // Aggregate data covering the period:
    // [start, start+buckets.size()*duration)
    // where duration is 1 second, 1 minute, 1 hour, or 1 day.
    std::deque <Bucket> buckets;
};

struct Histories
{
    std::array <History, resolutions.size()> data;
    std::vector <std::uint64_t> samples;

    Histories() {
      clock_type::time_point now = clock_type::now();
      for(auto i = data.begin();i != data.end(); i++) {
        i->start = now;
      }
    }
};

void addValue(Histories& hist, std::uint64_t const v);

void aggregateSamples(Histories& h, clock_type::time_point const now);

} // namespace impl
} // namespace metrics
} // namespace ripple

#endif // METRICS_HISTORY_H_INCLUDED
