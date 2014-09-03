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

#include "../History.h"

#include <beast/unit_test/suite.h>

#include <cassert>
#include <iostream>

namespace ripple {

namespace metrics {

namespace impl {

namespace test {

class history_test
    : public beast::unit_test::suite
{
public:
    void
    test_addValue ()
    {
        histories h;
        for (int i = 0; i < 1024; i++) {
            addValue (h, i);
        }

        for (int i = 0; i < 1024; i++) {
            assert(h.samples[i] == i);
        }

        pass ();
    }

    void
    test_aggregateRapidSamples ()
    {
        const int sampleCount = 1024;
        histories h;
        clock_type::time_point now = clock_type::now();
        h.data[0].start = now;

        for (int i = 0; i < sampleCount; i++) {
            addValue (h, i);
        }

        aggregateSamples (h, now);

        assert (h.samples.size() == 0);
        assert (h.data[0].buckets.size() == 1);
        assert (h.data[0].buckets[0].count == sampleCount);
        assert (h.data[0].buckets[0].min == 0);
        assert (h.data[0].buckets[0].max == sampleCount - 1);
        assert (h.data[0].buckets[0].avg == (sampleCount-1) / 2);

        pass ();
    }

    void
    test_aggregateTwoSeconds ()
    {
        const int sampleCount = 1024;
        histories h;
        clock_type::time_point now = clock_type::now();

        for (int i = 0; i < sampleCount; i++) {
            addValue (h, i);
        }

        aggregateSamples (h, now);

        for (int i = 0; i < sampleCount; i++) {
            addValue (h, i);
        }

        now += std::chrono::seconds(1);
        aggregateSamples (h, now);

        assert (h.samples.size() == 0);
        assert (h.data[0].buckets.size() == 2);
        assert (h.data[1].buckets.size() == 0);

        pass();
    }

    void
    test_aggregateOneMinute ()
    {
        const int sampleCount = 1024;
        histories h;
        clock_type::time_point now = h.data[0].start;

        for (int t = 0; t < 60; t++) {
          for (int i = 0; i < sampleCount; i++) {
              addValue (h, i);
          }

          aggregateSamples (h, now);
          now += std::chrono::seconds(1);
        }

        std::cout << h.data[0].buckets.size() << std::endl;
        assert (h.data[0].buckets.size() == 60);
        assert (h.data[1].buckets.size() == 1);

        pass();
    }

    void
    run ()
    {
        test_addValue ();
        test_aggregateRapidSamples ();
        test_aggregateTwoSeconds ();
        test_aggregateOneMinute ();
    }
};

BEAST_DEFINE_TESTSUITE (history, metrics, ripple);

} // namespace test

} // namespace impl

} // namespace metrics

} // namespace ripple
