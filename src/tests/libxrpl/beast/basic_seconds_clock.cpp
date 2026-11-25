#include <xrpl/beast/clock/basic_seconds_clock.h>

#include <gtest/gtest.h>

namespace beast {

// The clock keeps a lazily started background thread; this only asserts that
// asking it for the time does not throw or crash.
TEST(BasicSecondsClock, now)
{
    EXPECT_NO_THROW({ BasicSecondsClock::now(); });
}

}  // namespace beast
