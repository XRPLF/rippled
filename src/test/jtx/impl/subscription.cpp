//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <test/jtx/subscription.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {
namespace jtx {

/** Subscription operations. */
namespace subscription {

void
start_time::operator()(Env& env, JTx& jt) const
{
    jt.jv[sfStartTime.jsonName] = value_.time_since_epoch().count();
}

Json::Value
create(
    jtx::Account const& account,
    jtx::Account const& destination,
    STAmount const& amount,
    NetClock::duration const& frequency,
    std::optional<NetClock::time_point> const& expiration)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::SubscriptionSet;
    jv[jss::Account] = to_string(account.id());
    jv[jss::Destination] = to_string(destination.id());
    jv[jss::Amount] = amount.getJson(JsonOptions::none);
    jv[jss::Frequency] = frequency.count();
    jv[jss::Flags] = tfUniversal;
    if (expiration)
        jv[sfExpiration.jsonName] = expiration->time_since_epoch().count();
    return jv;
}

Json::Value
update(
    jtx::Account const& account,
    uint256 const& subscriptionId,
    STAmount const& amount,
    std::optional<NetClock::time_point> const& expiration)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::SubscriptionSet;
    jv[jss::Account] = to_string(account.id());
    jv[jss::SubscriptionID] = to_string(subscriptionId);
    jv[jss::Amount] = amount.getJson(JsonOptions::none);
    jv[jss::Flags] = tfUniversal;
    if (expiration)
        jv[sfExpiration.jsonName] = expiration->time_since_epoch().count();
    return jv;
}

Json::Value
cancel(jtx::Account const& account, uint256 const& subscriptionId)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::SubscriptionCancel;
    jv[jss::Account] = to_string(account.id());
    jv[jss::SubscriptionID] = to_string(subscriptionId);
    jv[jss::Flags] = tfUniversal;
    return jv;
}

Json::Value
claim(
    jtx::Account const& account,
    uint256 const& subscriptionId,
    STAmount const& amount)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::SubscriptionClaim;
    jv[jss::Account] = to_string(account.id());
    jv[jss::SubscriptionID] = to_string(subscriptionId);
    jv[jss::Amount] = amount.getJson(JsonOptions::none);
    jv[jss::Flags] = tfUniversal;
    return jv;
}

}  // namespace subscription

}  // namespace jtx

}  // namespace test
}  // namespace ripple
