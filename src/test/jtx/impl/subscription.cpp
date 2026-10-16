#include <test/jtx/subscription.h>

#include <test/jtx/Account.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <optional>

namespace xrpl::test::jtx::subscription {

void
StartTime::operator()(Env& env, JTx& jt) const
{
    jt.jv[sfStartTime.jsonName] = value_.time_since_epoch().count();
}

json::Value
create(
    jtx::Account const& account,
    jtx::Account const& destination,
    STAmount const& amount,
    NetClock::duration const& frequency,
    std::optional<NetClock::time_point> const& expiration,
    std::uint32_t flags)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::SubscriptionSet;
    jv[jss::Account] = to_string(account.id());
    jv[jss::Destination] = to_string(destination.id());
    jv[jss::Amount] = amount.getJson(JsonOptions::Values::None);
    jv[jss::Frequency] = frequency.count();
    jv[jss::Flags] = flags;
    if (expiration)
        jv[sfExpiration.jsonName] = expiration->time_since_epoch().count();
    return jv;
}

json::Value
update(
    jtx::Account const& account,
    uint256 const& subscriptionId,
    STAmount const& amount,
    std::optional<NetClock::time_point> const& expiration,
    std::optional<NetClock::duration> const& frequency)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::SubscriptionSet;
    jv[jss::Account] = to_string(account.id());
    jv[jss::SubscriptionID] = to_string(subscriptionId);
    jv[jss::Amount] = amount.getJson(JsonOptions::Values::None);
    jv[jss::Flags] = tfFullyCanonicalSig;
    if (expiration)
        jv[sfExpiration.jsonName] = expiration->time_since_epoch().count();
    if (frequency)
        jv[jss::Frequency] = frequency->count();
    return jv;
}

json::Value
cancel(jtx::Account const& account, uint256 const& subscriptionId)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::SubscriptionCancel;
    jv[jss::Account] = to_string(account.id());
    jv[jss::SubscriptionID] = to_string(subscriptionId);
    jv[jss::Flags] = tfFullyCanonicalSig;
    return jv;
}

json::Value
claim(jtx::Account const& account, uint256 const& subscriptionId, STAmount const& amount)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::SubscriptionClaim;
    jv[jss::Account] = to_string(account.id());
    jv[jss::SubscriptionID] = to_string(subscriptionId);
    jv[jss::Amount] = amount.getJson(JsonOptions::Values::None);
    jv[jss::Flags] = tfFullyCanonicalSig;
    return jv;
}

}  // namespace xrpl::test::jtx::subscription
