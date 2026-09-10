
#include <xrpld/rpc/detail/Handler.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/ApiVersion.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <random>
#include <string>
#include <tuple>
// cspell: words stdev

namespace xrpl::test {

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
// dissimilar to how xrpld will normally work.
// TODO as https://github.com/XRPLF/rippled/issues/4765

class Handler_test : public beast::unit_test::Suite
{
    auto
    time(std::size_t n, auto f, auto prng) -> auto
    {
        using clock = std::chrono::steady_clock;
        assert(n > 0);
        double sum = 0;
        double sumSquared = 0;
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

            std::ranges::sort(samples);
            for (std::size_t k = 35; k < 65; ++k)
            {
                j += 1;
                sum += samples[k];
                sumSquared += (samples[k] * samples[k]);
            }
        }

        double const meanSquared = (sum * sum) / (j * j);
        return std::make_tuple(
            clock::duration{static_cast<long>(sum / j)},
            clock::duration{static_cast<long>(std::sqrt((sumSquared / j) - meanSquared))},
            j);
    }

    void
    reportLookupPerformance()
    {
        testcase("Handler lookup performance");

        std::random_device dev;
        std::ranlux48 prng(dev());

        // Contiguous, so the timed loop's pick-a-name-by-index costs nothing
        // and the measurement reflects getHandler() alone.
        auto const names = xrpl::rpc::getHandlerNames();

        std::uniform_int_distribution<std::size_t> distr{0, names.size() - 1};

        // The lowest version still served. Asking for one outside the supported
        // range would make getHandler() return at its bounds check, without
        // searching, and the benchmark would then be timing that check.
        constexpr unsigned kVersion = rpc::kApiMinimumSupportedVersion;

        std::size_t dummy = 0;
        std::size_t misses = 0;
        auto const [mean, stdev, n] = time(
            1'000'000,
            [&](std::size_t i) {
                auto const d = rpc::getHandler(kVersion, false, names[i]);
                if (d == nullptr)
                {
                    ++misses;
                    return;
                }
                dummy = dummy + i + (int)d->role;
            },
            [&]() -> std::size_t { return distr(prng); });

        std::cout << "mean=" << mean << " stdev=" << stdev << " N=" << n << '\n';

        // A miss means the timed call did no lookup, so the figure above is not
        // a measurement of one. Every name comes from getHandlerNames(), so a
        // handler answering at kVersion is the only way this holds.
        BEAST_EXPECTS(
            misses == 0,
            std::to_string(misses) + " of " + std::to_string(n) + " lookups at API version " +
                std::to_string(kVersion) + " found no handler, so nothing was measured");
        BEAST_EXPECT(dummy != 0);
    }

public:
    void
    run() override
    {
        reportLookupPerformance();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(Handler, rpc, xrpl);

}  // namespace xrpl::test
