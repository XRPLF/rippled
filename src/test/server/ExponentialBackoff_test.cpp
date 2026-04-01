#include <xrpl/beast/unit_test.h>
#include <xrpl/server/detail/ExponentialBackoff.h>

namespace xrpl {

class ExponentialBackoff_test : public beast::unit_test::suite
{
public:
    void
    testDefaultConstruction()
    {
        testcase("default construction");

        ExponentialBackoff backoff;

        BEAST_EXPECT(backoff.initial() == ExponentialBackoff::DEFAULT_INITIAL_DELAY);
        BEAST_EXPECT(backoff.maximum() == ExponentialBackoff::DEFAULT_MAX_DELAY);
        BEAST_EXPECT(backoff.current() == ExponentialBackoff::DEFAULT_INITIAL_DELAY);
    }

    void
    testCustomConstruction()
    {
        testcase("custom construction");

        using namespace std::chrono_literals;

        ExponentialBackoff backoff{100ms, 5000ms};

        BEAST_EXPECT(backoff.initial() == 100ms);
        BEAST_EXPECT(backoff.maximum() == 5000ms);
        BEAST_EXPECT(backoff.current() == 100ms);
    }

    void
    testIncreaseDoublesDelay()
    {
        testcase("increase doubles delay");

        using namespace std::chrono_literals;

        ExponentialBackoff backoff{50ms, 2000ms};

        BEAST_EXPECT(backoff.current() == 50ms);

        auto delay = backoff.increase();
        BEAST_EXPECT(delay == 100ms);
        BEAST_EXPECT(backoff.current() == 100ms);

        delay = backoff.increase();
        BEAST_EXPECT(delay == 200ms);
        BEAST_EXPECT(backoff.current() == 200ms);

        delay = backoff.increase();
        BEAST_EXPECT(delay == 400ms);
        BEAST_EXPECT(backoff.current() == 400ms);

        delay = backoff.increase();
        BEAST_EXPECT(delay == 800ms);
        BEAST_EXPECT(backoff.current() == 800ms);

        delay = backoff.increase();
        BEAST_EXPECT(delay == 1600ms);
        BEAST_EXPECT(backoff.current() == 1600ms);
    }

    void
    testIncreaseCapsAtMaximum()
    {
        testcase("increase caps at maximum");

        using namespace std::chrono_literals;

        ExponentialBackoff backoff{50ms, 2000ms};

        // Increase until we hit the cap
        for (int i = 0; i < 10; ++i)
        {
            backoff.increase();
        }

        // Should be capped at maximum
        BEAST_EXPECT(backoff.current() == 2000ms);

        // Further increases should not exceed maximum
        auto delay = backoff.increase();
        BEAST_EXPECT(delay == 2000ms);
        BEAST_EXPECT(backoff.current() == 2000ms);
    }

    void
    testResetReturnsToInitial()
    {
        testcase("reset returns to initial");

        using namespace std::chrono_literals;

        ExponentialBackoff backoff{50ms, 2000ms};

        // Increase several times
        backoff.increase();
        backoff.increase();
        backoff.increase();
        BEAST_EXPECT(backoff.current() == 400ms);

        // Reset should return to initial
        auto delay = backoff.reset();
        BEAST_EXPECT(delay == 50ms);
        BEAST_EXPECT(backoff.current() == 50ms);
    }

    void
    testTypicalDoorUsage()
    {
        testcase("typical door usage pattern");

        using namespace std::chrono_literals;

        // Simulates Door's usage pattern
        ExponentialBackoff backoff{50ms, 2000ms};

        // First throttle
        BEAST_EXPECT(backoff.current() == 50ms);
        backoff.increase();
        BEAST_EXPECT(backoff.current() == 100ms);

        // Second throttle
        backoff.increase();
        BEAST_EXPECT(backoff.current() == 200ms);

        // Success - reset
        backoff.reset();
        BEAST_EXPECT(backoff.current() == 50ms);

        // Another throttle sequence
        backoff.increase();
        BEAST_EXPECT(backoff.current() == 100ms);
        backoff.increase();
        BEAST_EXPECT(backoff.current() == 200ms);
        backoff.increase();
        BEAST_EXPECT(backoff.current() == 400ms);

        // Success - reset
        backoff.reset();
        BEAST_EXPECT(backoff.current() == 50ms);
    }

    void
    run() override
    {
        testDefaultConstruction();
        testCustomConstruction();
        testIncreaseDoublesDelay();
        testIncreaseCapsAtMaximum();
        testResetReturnsToInitial();
        testTypicalDoorUsage();
    }
};

BEAST_DEFINE_TESTSUITE(ExponentialBackoff, server, xrpl);

}  // namespace xrpl
