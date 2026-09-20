
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
#include <iterator>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>
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

        // The lowest version still served. Outside the supported range getHandler()
        // returns at its bounds check without searching, so the benchmark would
        // time that check instead of a lookup.
        constexpr unsigned kVersion = rpc::kApiMinimumSupportedVersion;

        // Only the names that answer at kVersion, so that every timed call does a
        // whole lookup: a method served from a later version only would not.
        // Contiguous, so that picking one by index costs nothing.
        std::vector<std::string_view> names;
        std::ranges::copy_if(
            rpc::getHandlerNames(), std::back_inserter(names), [](std::string_view name) {
                return rpc::getHandler(kVersion, false, name) != nullptr;
            });

        if (!BEAST_EXPECTS(
                !names.empty(),
                "no handler answers at API version " + std::to_string(kVersion) +
                    ", so there is nothing to measure"))
            return;

        std::uniform_int_distribution<std::size_t> distr{0, names.size() - 1};

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

        // Every name answered once already, so a miss here cannot happen.
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

// Manual: the suite only reports a timing, which says nothing on a CI runner.
// The table invariants are static_asserts in Handler.cpp.
BEAST_DEFINE_TESTSUITE_MANUAL(Handler, rpc, xrpl);

// What getHandler() answers, as opposed to how fast it answers. A lookup needs no
// Application, so these cases run as an automatic suite.
//
// The bounds check they cover is unreachable from a request: getAPIVersionNumber()
// applies the same predicate first, and every caller rejects an invalid version
// before it asks for a handler. That is why it is checked here directly, and why
// it is worth checking at all rather than deleting as unreachable.
class HandlerLookup_test : public beast::unit_test::Suite
{
    /**
     * Find a method that is served at a given API version.
     *
     * The name comes from the table, so a case below does not name a method that a
     * later API version may retire.
     *
     * @param version The API version to answer at.
     * @param betaEnabled Whether the beta API version is enabled.
     * @return A name that answers, or nullopt if none does.
     */
    static std::optional<std::string_view>
    nameServedAt(unsigned version, bool betaEnabled)
    {
        for (std::string_view name : rpc::getHandlerNames())
        {
            if (rpc::getHandler(version, betaEnabled, name) != nullptr)
                return name;
        }

        return std::nullopt;
    }

    void
    testUnservedVersion()
    {
        testcase("An unserved API version has no handler");

        // A name the table certainly holds, so that a null answer below can only
        // come from the version and not from the name.
        auto const name = nameServedAt(rpc::kApiMinimumSupportedVersion, false);
        if (!BEAST_EXPECTS(
                name.has_value(),
                "no handler answers at API version " +
                    std::to_string(rpc::kApiMinimumSupportedVersion) +
                    ", so there is no name to ask about"))
            return;

        // Below the minimum, which no setting serves.
        BEAST_EXPECT(
            rpc::getHandler(rpc::kApiMinimumSupportedVersion - 1, false, *name) == nullptr);
        BEAST_EXPECT(rpc::getHandler(rpc::kApiMinimumSupportedVersion - 1, true, *name) == nullptr);

        // Above the maximum each setting serves. Both values stay outside the
        // served range however the version constants move, so neither case can
        // become vacuous.
        BEAST_EXPECT(
            rpc::getHandler(rpc::kApiMaximumSupportedVersion + 1, false, *name) == nullptr);
        BEAST_EXPECT(rpc::getHandler(rpc::kApiBetaVersion + 1, true, *name) == nullptr);
    }

    void
    testBetaVersionGate()
    {
        testcase("The beta API version is served only where it is enabled");

        // Between betas the beta version is the maximum supported one, leaving the
        // two settings nothing to tell apart. Compiled out rather than asserted, so
        // that the case arms itself again when a later beta version arrives.
        if constexpr (rpc::kApiBetaVersion > rpc::kApiMaximumSupportedVersion)
        {
            auto const name = nameServedAt(rpc::kApiBetaVersion, true);
            if (!BEAST_EXPECTS(
                    name.has_value(),
                    "no handler answers at API version " + std::to_string(rpc::kApiBetaVersion) +
                        ", so there is nothing for the gate to reject"))
                return;

            // The handler serves this version, so only the server's own range can
            // turn the answer into a null one.
            BEAST_EXPECT(rpc::getHandler(rpc::kApiBetaVersion, true, *name) != nullptr);
            BEAST_EXPECT(rpc::getHandler(rpc::kApiBetaVersion, false, *name) == nullptr);
        }
        else
        {
            log << "the beta API version is the maximum supported version, so no gate "
                   "separates them\n";
            pass();
        }
    }

    void
    testUnknownMethod()
    {
        testcase("An unknown method has no handler");

        constexpr unsigned kVersion = rpc::kApiMinimumSupportedVersion;

        BEAST_EXPECT(rpc::getHandler(kVersion, false, "no such method") == nullptr);
        BEAST_EXPECT(rpc::getHandler(kVersion, false, "") == nullptr);

        // A method name holds lowercase letters and underscores, so a tilde sorts
        // after every entry. This runs the search off the end of the table, which
        // no other case here does.
        BEAST_EXPECT(rpc::getHandler(kVersion, false, "~") == nullptr);
    }

public:
    void
    run() override
    {
        testUnservedVersion();
        testBetaVersionGate();
        testUnknownMethod();
    }
};

BEAST_DEFINE_TESTSUITE(HandlerLookup, rpc, xrpl);

}  // namespace xrpl::test
