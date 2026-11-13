#include <test/fuzz/FuzzCheck.h>
#include <test/fuzz/FuzzTargets.h>

#include <xrpl/beast/unit_test/suite.h>

#include <cstddef>
#include <string>

namespace xrpl::fuzz {

/**
 * @brief Run every fuzz target's seeds through the target.
 *
 * This keeps the targets building and their seeds passing in the normal unit-test run,
 * on every compiler and platform, without libFuzzer.
 */
class FuzzReplay_test : public beast::unit_test::Suite
{
    void
    replay(FuzzTarget const& target)
    {
        testcase(std::string(target.name));

        auto const seeds = target.seeds();
        BEAST_EXPECT(!seeds.empty());
        for (std::size_t i = 0; i < seeds.size(); ++i)
        {
            try
            {
                BEAST_EXPECT(target.run(seeds[i].data(), seeds[i].size()) == 0);
            }
            catch (OracleFailure const& f)
            {
                fail("seed " + std::to_string(i) + ": " + f.what);
            }
        }
    }

public:
    void
    run() override
    {
        for (auto const& target : fuzzTargets())
            replay(target);
    }
};

BEAST_DEFINE_TESTSUITE(FuzzReplay, fuzz, xrpl);

}  // namespace xrpl::fuzz
