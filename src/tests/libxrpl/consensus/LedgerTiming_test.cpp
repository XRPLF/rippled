#include <xrpl/basics/chrono.h>
#include <xrpl/ledger/LedgerTiming.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <utility>

namespace xrpl::test {

class LedgerTimingTest : public ::testing::Test
{
protected:
    static void
    testGetNextLedgerTimeResolution()
    {
        // helper to iteratively call into getNextLedgerTimeResolution
        struct TestRes
        {
            std::uint32_t decrease = 0;
            std::uint32_t equal = 0;
            std::uint32_t increase = 0;

            static TestRes
            run(bool previousAgree, std::uint32_t rounds)
            {
                TestRes res;
                auto closeResolution = kLEDGER_DEFAULT_TIME_RESOLUTION;
                auto nextCloseResolution = closeResolution;
                std::uint32_t round = 0;
                do
                {
                    nextCloseResolution =
                        getNextLedgerTimeResolution(closeResolution, previousAgree, ++round);
                    if (nextCloseResolution < closeResolution)
                    {
                        ++res.decrease;
                    }
                    else if (nextCloseResolution > closeResolution)
                    {
                        ++res.increase;
                    }
                    else
                    {
                        ++res.equal;
                    }
                    std::swap(nextCloseResolution, closeResolution);
                } while (round < rounds);
                return res;
            }
        };

        // If we never agree on close time, only can increase resolution
        // until hit the max
        auto decreases = TestRes::run(false, 10);
        EXPECT_TRUE(decreases.increase == 3);
        EXPECT_TRUE(decreases.decrease == 0);
        EXPECT_TRUE(decreases.equal == 7);

        // If we always agree on close time, only can decrease resolution
        // until hit the min
        auto increases = TestRes::run(false, 100);
        EXPECT_TRUE(increases.increase == 3);
        EXPECT_TRUE(increases.decrease == 0);
        EXPECT_TRUE(increases.equal == 97);
    }

    static void
    testRoundCloseTime()
    {
        using namespace std::chrono_literals;
        // A closeTime equal to the epoch is not modified
        using tp = NetClock::time_point;
        tp const def;
        EXPECT_TRUE(def == roundCloseTime(def, 30s));

        // Otherwise, the closeTime is rounded to the nearest
        // rounding up on ties
        EXPECT_TRUE(tp{0s} == roundCloseTime(tp{29s}, 60s));
        EXPECT_TRUE(tp{30s} == roundCloseTime(tp{30s}, 1s));
        EXPECT_TRUE(tp{60s} == roundCloseTime(tp{31s}, 60s));
        EXPECT_TRUE(tp{60s} == roundCloseTime(tp{30s}, 60s));
        EXPECT_TRUE(tp{60s} == roundCloseTime(tp{59s}, 60s));
        EXPECT_TRUE(tp{60s} == roundCloseTime(tp{60s}, 60s));
        EXPECT_TRUE(tp{60s} == roundCloseTime(tp{61s}, 60s));
    }

    static void
    testEffCloseTime()
    {
        using namespace std::chrono_literals;
        using tp = NetClock::time_point;
        tp close = effCloseTime(tp{10s}, 30s, tp{0s});
        EXPECT_TRUE(close == tp{1s});

        close = effCloseTime(tp{16s}, 30s, tp{0s});
        EXPECT_TRUE(close == tp{30s});

        close = effCloseTime(tp{16s}, 30s, tp{30s});
        EXPECT_TRUE(close == tp{31s});

        close = effCloseTime(tp{16s}, 30s, tp{60s});
        EXPECT_TRUE(close == tp{61s});

        close = effCloseTime(tp{31s}, 30s, tp{0s});
        EXPECT_TRUE(close == tp{30s});
    }

    void
    run()
    {
        testGetNextLedgerTimeResolution();
        testRoundCloseTime();
        testEffCloseTime();
    }
};

TEST_F(LedgerTimingTest, ledger_timing)
{
    run();
}
}  // namespace xrpl::test
