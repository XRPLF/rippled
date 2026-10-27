#include <xrpl/server/InfoSub.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ApiVersion.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>

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

// A message sent to an api-3 subscriber becomes a notification: `type` names the method and the
// rest of the message becomes `params`.
TEST(InfoSubNotification, shapes_message_from_spec_version)
{
    json::Value event(json::ValueType::Object);
    event[jss::type] = "ledgerClosed";
    event[jss::ledger_index] = 3;

    auto const shaped = shapeAsNotification(event, rpc::kApiMinimumSpecVersion);
    ASSERT_TRUE(shaped.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) asserted above
    json::Value const& notification = *shaped;
    // The literal the document shows, not the constant production writes.
    EXPECT_EQ(notification[jss::jsonrpc], "2.0");
    EXPECT_EQ(notification[jss::method], "ledgerClosed");
    EXPECT_EQ(notification[jss::params][jss::ledger_index], 3);
    // Exactly the three members: no id, no `type`, and nothing of a reply's envelope.
    EXPECT_EQ(notification.size(), 3u);
    EXPECT_FALSE(notification.isMember(jss::id));
    EXPECT_FALSE(notification[jss::params].isMember(jss::type));
}

// A path finding update is a notification too, for the reason shapeAsNotification's docstring
// gives, so the `id` the client correlates by has to survive inside `params`.
TEST(InfoSubNotification, shapes_a_directed_update)
{
    json::Value update(json::ValueType::Object);
    update[jss::type] = "path_find";
    update[jss::id] = 7;
    update[jss::full_reply] = true;

    auto const shaped = shapeAsNotification(update, rpc::kApiMinimumSpecVersion);
    ASSERT_TRUE(shaped.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) asserted above
    json::Value const& notification = *shaped;
    EXPECT_EQ(notification[jss::method], "path_find");
    EXPECT_FALSE(notification.isMember(jss::id));
    EXPECT_EQ(notification[jss::params][jss::id], 7);
    EXPECT_EQ(notification[jss::params][jss::full_reply], true);
}

TEST(InfoSubNotification, leaves_everything_else_alone)
{
    json::Value event(json::ValueType::Object);
    event[jss::type] = "ledgerClosed";

    // Below the specification version the shape is the legacy one.
    EXPECT_FALSE(shapeAsNotification(event, 1).has_value());
    EXPECT_FALSE(shapeAsNotification(event, 2).has_value());
    // A version is recorded at subscribe time; 0 means it was never recorded.
    EXPECT_FALSE(shapeAsNotification(event, 0).has_value());
    // Every version from 3 up shapes, not version 3 alone.
    EXPECT_TRUE(shapeAsNotification(event, 4).has_value());

    // A message naming no `type` names no method to dispatch on. rpcError builds one: the caller
    // that reports a failure this way names the stream it concerns before sending it.
    EXPECT_FALSE(
        shapeAsNotification(rpcError(RpcInternal), rpc::kApiMinimumSpecVersion).has_value());

    // A `type` that is not a string cannot be a `method`, so it names no method either.
    json::Value numbered(json::ValueType::Object);
    numbered[jss::type] = 1;
    EXPECT_FALSE(shapeAsNotification(numbered, rpc::kApiMinimumSpecVersion).has_value());

    // A non-object has no members to move under `params`.
    EXPECT_FALSE(shapeAsNotification(json::Value{"text"}, rpc::kApiMinimumSpecVersion).has_value());
}

// A shaped message names no `type`, so shaping it again returns nullopt. Pinned so a message can
// never be wrapped twice.
TEST(InfoSubNotification, shapes_a_message_only_once)
{
    json::Value event(json::ValueType::Object);
    event[jss::type] = "ledgerClosed";
    event[jss::ledger_index] = 3;

    auto const shaped = shapeAsNotification(event, rpc::kApiMinimumSpecVersion);
    ASSERT_TRUE(shaped.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) asserted above
    EXPECT_FALSE(shapeAsNotification(*shaped, rpc::kApiMinimumSpecVersion).has_value());
}

// An account_history subscription reports a stream failure as an error object, which
// stampStreamError names after the stream for a subscriber a notification is built for, so the
// failure is a notification like any other event on that stream. The three sites that send one
// need a failing relational database to reach, so the stamping and the shaping are asserted here.
TEST(InfoSubNotification, stamps_a_stream_failure_for_a_notification_subscriber)
{
    json::Value const failure = rpcError(RpcInternal);

    auto const stamped = stampStreamError(
        failure, rpc::kApiMinimumSpecVersion, true, jss::account_history_tx_stream);
    ASSERT_TRUE(stamped.has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access) asserted above
    EXPECT_EQ((*stamped)[jss::type], jss::account_history_tx_stream);
    EXPECT_EQ((*stamped)[jss::error], "internal");
    // The input is left as it was: the copy is the stamped one.
    EXPECT_FALSE(failure.isMember(jss::type));

    auto const shaped = shapeAsNotification(*stamped, rpc::kApiMinimumSpecVersion);
    // NOLINTEND(bugprone-unchecked-optional-access)
    ASSERT_TRUE(shaped.has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access) asserted above
    EXPECT_EQ((*shaped)[jss::method], jss::account_history_tx_stream);
    EXPECT_EQ((*shaped)[jss::params][jss::error], "internal");
    EXPECT_EQ((*shaped)[jss::params][jss::error_code], RpcInternal);
    EXPECT_FALSE(shaped->isMember(jss::id));
    // NOLINTEND(bugprone-unchecked-optional-access)
}

// Below version 3, and for a subscriber that cannot receive a notification, the error goes out as
// it stands: no copy and no `type`, which an unshaped object would carry onto the wire.
TEST(InfoSubNotification, leaves_a_stream_failure_alone_otherwise)
{
    json::Value const failure = rpcError(RpcInternal);

    EXPECT_FALSE(stampStreamError(failure, 1, true, jss::account_history_tx_stream).has_value());
    EXPECT_FALSE(stampStreamError(failure, 2, true, jss::account_history_tx_stream).has_value());
    EXPECT_FALSE(stampStreamError(
                     failure, rpc::kApiMinimumSpecVersion, false, jss::account_history_tx_stream)
                     .has_value());
}
