#include <xrpl/server/InfoSub.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <limits>

using namespace xrpl;

// The per-connection subscription cap is enforced by the pure predicate
// exceedsSubscriptionCap(current, additional). Testing it directly (rather than
// by subscribing the real cap through a WebSocket, which would exceed the frame
// limit and drop the connection before the check runs) lets the boundary be
// asserted exactly.
TEST(InfoSubSubscriptionCap, boundary)
{
    constexpr std::size_t cap = kMaxSubscriptionsPerConnection;

    // Empty connection: anything up to the cap is admitted, cap+1 is not.
    EXPECT_FALSE(exceedsSubscriptionCap(0, 0));
    EXPECT_FALSE(exceedsSubscriptionCap(0, cap));
    EXPECT_TRUE(exceedsSubscriptionCap(0, cap + 1));

    // Exactly at the cap: zero more is fine, one more is rejected.
    EXPECT_FALSE(exceedsSubscriptionCap(cap, 0));
    EXPECT_TRUE(exceedsSubscriptionCap(cap, 1));

    // One below the cap: exactly one more reaches the cap; two exceed it.
    EXPECT_FALSE(exceedsSubscriptionCap(cap - 1, 1));
    EXPECT_TRUE(exceedsSubscriptionCap(cap - 1, 2));
}

TEST(InfoSubSubscriptionCap, no_overflow)
{
    constexpr std::size_t cap = kMaxSubscriptionsPerConnection;
    constexpr std::size_t max = std::numeric_limits<std::size_t>::max();

    // current + additional must not wrap: a huge additional is rejected even
    // when current is 0 (the additional > cap term guards the subtraction).
    EXPECT_TRUE(exceedsSubscriptionCap(0, max));
    EXPECT_TRUE(exceedsSubscriptionCap(cap, max));
}

TEST(InfoSubSubscriptionCap, explicit_cap)
{
    // A configured override is honored: the boundary tracks the passed cap, not
    // the built-in default. This is the seam doSubscribe uses to enforce a
    // per-connection cap set via [max_subscriptions_per_connection].
    constexpr std::size_t cap = 5;

    EXPECT_FALSE(exceedsSubscriptionCap(0, cap, cap));
    EXPECT_TRUE(exceedsSubscriptionCap(0, cap + 1, cap));
    EXPECT_FALSE(exceedsSubscriptionCap(cap, 0, cap));
    EXPECT_TRUE(exceedsSubscriptionCap(cap, 1, cap));
    EXPECT_FALSE(exceedsSubscriptionCap(cap - 1, 1, cap));
    EXPECT_TRUE(exceedsSubscriptionCap(cap - 1, 2, cap));

    // The overflow guard still holds with a small explicit cap.
    EXPECT_TRUE(exceedsSubscriptionCap(0, std::numeric_limits<std::size_t>::max(), cap));
}
