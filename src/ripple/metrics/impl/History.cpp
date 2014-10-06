//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2014 Ripple Labs Inc.

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

#include <ripple/metrics/impl/History.h>
#include <map>
#include <type_traits>
#include <iostream>
#include <chrono>

namespace ripple {
namespace metrics {
namespace impl {


// Maps metric names to their available history
std::map <std::string, Histories> histories_;

void addValue(Histories& hist, std::uint64_t const v)
{
    hist.samples.push_back(v);
    aggregateSamples (hist, clock_type::now());
}

std::vector<Bucket>
getBuckets(clock_type::time_point const start,
           clock_type::time_point const last,
           History const& history,
           clock_type::duration const& duration);

// Produces one bucket from a sequence of many
template <class FwdIt>
Bucket
aggregate (FwdIt const& first, FwdIt const& last)
{
    Bucket newBucket = {};
    std::uint64_t sum = 0;

    if (first == last)
      return newBucket;

    newBucket.min = first->min;

    for (auto i = first; i != last; i++) {
        sum += i->avg;
        newBucket.count += i->count;
        newBucket.min = std::min (i->min, newBucket.min);
        newBucket.max = std::max (i->max, newBucket.max);
    }

    if (newBucket.count > 0)
        newBucket.avg = sum / newBucket.count;

    return newBucket;
}

static void
aggregateBucket(History& from, resolution const& from_res, History& to,
    resolution const& to_res)
{
    clock_type::time_point windowStartTime (to.start);
    clock_type::time_point windowEndTime (windowStartTime - to_res.duration);
    std::size_t bucketCount = to_res.duration.count() / from_res.duration.count();
    std::size_t availableBuckets = (from.start - (windowStartTime - from_res.duration*2)) / from_res.duration;


    if (availableBuckets >= bucketCount) {
      from.buckets.resize (std::max (availableBuckets, from.buckets.size()));
      std::size_t offsetIdx = availableBuckets % bucketCount;
      auto end = from.buckets.begin();
      auto start = end;
      std::advance (end, availableBuckets);
      std::advance (start, availableBuckets + offsetIdx - bucketCount);

      to.buckets.push_front (aggregate (start, end));
      to.start += from_res.duration;
    }
}

void
aggregateSamples(Histories& h, clock_type::time_point const now)
{
    Bucket newSecond = {};
    std::uint64_t sum = 0;

    for (int i = 0; i < resolutions.size()-1; i++) {
        aggregateBucket (h.data[i], resolutions[i], h.data[i+1], resolutions[i+1]);
        if (h.data[i].buckets.size() > 300)
          h.data[i].buckets.resize(300);
    }

    if (h.samples.size() > 0) {
        newSecond.min = h.samples[0];
        newSecond.count = h.samples.size();

        // Bundle all the samples together
        for (auto i = h.samples.cbegin(); i != h.samples.cend();i++) {
            sum += *i;
            newSecond.min = std::min (*i, newSecond.min);
            newSecond.max = std::max (*i, newSecond.max);
        }

        h.samples.clear();

        newSecond.avg = sum / newSecond.count;
        h.data[0].buckets.push_front (newSecond);
        h.data[0].start = now;
    }
}

} // namespace impl
} // namespace metrics
} // namespace ripple
