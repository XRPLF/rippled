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

#include <ripple/beast/unit_test.h>
#include <ripple/rpc/impl/Handler.h>
#include <test/jtx.h>

#include <chrono>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>

namespace ripple::test {

// NOTE: there should be no need for this function;
// `std::cout << some_duration` should just work if built with a compliant
// C++20 compiler. Sadly, we are not using one, as of today
// TODO: remove this operator<< overload when we bump compiler version
std::ostream&
operator<<(std::ostream& os, std::chrono::nanoseconds ns)
{
    return (os << ns.count() << "ns");
}

// NOTE This is a rather naive effort at a microbenchmark. Ideally we want
// Google Benchmark, or something similar. Also, this actually does not belong
// to unit tests, as it makes little sense to run it in conditions very
// dissimilar to how rippled will normally work.
// TODO as https://github.com/XRPLF/rippled/issues/4765

class Handler_test : public beast::unit_test::suite
{
    auto
    time(std::size_t n, auto f, auto prng) -> auto
    {
        using clock = std::chrono::steady_clock;
        assert(n > 0);
        double sum = 0;
        double sum_squared = 0;
        std::size_t j = 0;
        while (j < n)
        {
            // Generate 100 inputs upfront, separated from the inner loop
            std::array<decltype(prng()), 100> inputs = {};
            for (auto& i : inputs)
            {
                i = prng();
            }

            // Take 100 samples, then sort and throw away 35 from each end,
            // using only middle 30. This helps to reduce measurement noise.
            std::array<long, 100> samples = {};
            for (std::size_t k = 0; k < 100; ++k)
            {
                auto start = std::chrono::steady_clock::now();
                f(inputs[k]);
                samples[k] = (std::chrono::steady_clock::now() - start).count();
            }

            std::sort(samples.begin(), samples.end());
            for (std::size_t k = 35; k < 65; ++k)
            {
                j += 1;
                sum += samples[k];
                sum_squared += (samples[k] * samples[k]);
            }
        }

        const double mean_squared = (sum * sum) / (j * j);
        return std::make_tuple(
            clock::duration{static_cast<long>(sum / j)},
            clock::duration{
                static_cast<long>(std::sqrt((sum_squared / j) - mean_squared))},
            j);
    }

    void
    reportLookupPerformance()
    {
        testcase("Handler lookup performance");

        std::random_device dev;
        std::ranlux48 prng(dev());

        std::vector<const char*> names =
            test::jtx::make_vector(ripple::RPC::getHandlerNames());

        std::uniform_int_distribution<std::size_t> distr{0, names.size() - 1};

        std::size_t dummy = 0;
        auto const [mean, stdev, n] = time(
            1'000'000,
            [&](std::size_t i) {
                auto const d = RPC::getHandler(1, false, names[i]);
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

BEAST_DEFINE_TESTSUITE_MANUAL(Handler, test, ripple);

}  // namespace ripple::test
