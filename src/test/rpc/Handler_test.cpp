//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#include <ripple/rpc/impl/Handler.h>
#include "ripple/overlay/Peer.h"
#include <limits>
#include <test/jtx.h>

#include <chrono>
#include <iostream>
#include <numeric>
#include <random>

namespace ripple::RPC {

// NOTE This is a rather naiive effort at a microbenchmark. Ideally we want
// Google Benchmark, or something similar. Also, this actually does not belong
// to unit tests, as it makes little sense to run it in conditions very
// dissimilar to how rippled will normally work.
// TODO as https://github.com/XRPLF/rippled/issues/4765

class Handler_test : public beast::unit_test::suite
{
    std::tuple<double, double, std::size_t>
    time(std::size_t n, auto f, auto prng)
    {
        assert(n > 0);
        double sum = 0;
        double sum_squared = 0;
        std::size_t j = 0;
        while (j < n)
        {
            // Generate 20 inputs upfront, separated from the inner loop
            std::array<decltype(prng()), 20> inputs = {};
            for (auto& i : inputs)
            {
                i = prng();
            }

            // Take 20 samples and throw away 7 from each end, using middle 6
            std::array<long, 20> samples = {};
            for (std::size_t k = 0; k < 20; ++k)
            {
                auto start = std::chrono::steady_clock::now();
                f(inputs[k]);
                samples[k] = (std::chrono::steady_clock::now() - start).count();
            }

            std::sort(samples.begin(), samples.end());
            for (std::size_t k = 7; k < 13; ++k)
            {
                j += 1;
                sum += samples[k];
                sum_squared += (samples[k] * samples[k]);
            }
        }

        double const mean = sum / j;
        return {mean, std::sqrt((sum_squared / j) - (mean * mean)), j};
    }

    void
    reportLookupPerformance()
    {
        testcase("Handler lookup performance");

        std::random_device dev;
        std::ranlux48 prng(dev());

        auto const names = getHandlerNames();
        std::uniform_int_distribution<std::size_t> distr{0, names.size() - 1};

        std::size_t dummy = 0;
        auto const [mean, stdev, n] = time(
            1'000'000,
            [&](std::size_t i) {
                auto const d = getHandler(1, false, names[i]);
                dummy = dummy + i + (int)d->role_;
            },
            [&]() -> std::size_t { return distr(prng); });

        std::cout << "mean=" << mean << " stdev=" << stdev << " N=" << n
                  << '\n';

        BEAST_EXPECT(dummy != 0);
    }

public:
    void
    run() override
    {
        reportLookupPerformance();
    }
};

BEAST_DEFINE_TESTSUITE(Handler, rpc, ripple);

}  // namespace ripple::RPC