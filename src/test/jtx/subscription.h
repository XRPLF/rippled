#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TxFlags.h>

#include <optional>

namespace xrpl::test::jtx {

/** Subscription operations. */
namespace subscription {

/** Create a subscription. Pass a frequency of zero for an unmetered
    subscription and set tfSingleUse in flags for a one-shot subscription. */
json::Value
create(
    jtx::Account const& account,
    jtx::Account const& destination,
    STAmount const& amount,
    NetClock::duration const& frequency,
    std::optional<NetClock::time_point> const& expiration = std::nullopt,
    std::uint32_t flags = tfFullyCanonicalSig);

/** Update a subscription. An engaged expiration of zero removes any existing
    expiration; an engaged frequency changes it and resets the period. */
json::Value
update(
    jtx::Account const& account,
    uint256 const& subscriptionId,
    STAmount const& amount,
    std::optional<NetClock::time_point> const& expiration = std::nullopt,
    std::optional<NetClock::duration> const& frequency = std::nullopt);

/** Cancel a subscription. */
json::Value
cancel(jtx::Account const& account, uint256 const& subscriptionId);

/** Claim a subscription payment. */
json::Value
claim(jtx::Account const& account, uint256 const& subscriptionId, STAmount const& amount);

/** Set the "StartTime" time tag on a JTx. */
class StartTime
{
private:
    NetClock::time_point value_;

public:
    explicit StartTime(NetClock::time_point const& value) : value_(value)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

}  // namespace subscription

}  // namespace xrpl::test::jtx
