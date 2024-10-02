//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2019 Ripple Labs Inc.

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

#ifndef RIPPLE_TEST_JTX_SUBSCRIPTION_H_INCLUDED
#define RIPPLE_TEST_JTX_SUBSCRIPTION_H_INCLUDED

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>

namespace ripple {
namespace test {
namespace jtx {

/** Subscription operations. */
namespace subscription {

Json::Value
create(
    jtx::Account const& account,
    jtx::Account const& destination,
    STAmount const& amount,
    NetClock::duration const& frequency,
    std::optional<NetClock::time_point> const& expiration = std::nullopt);

Json::Value
update(
    jtx::Account const& account,
    uint256 const& subscriptionId,
    STAmount const& amount,
    std::optional<NetClock::time_point> const& expiration = std::nullopt);

Json::Value
cancel(jtx::Account const& account, uint256 const& subscriptionId);

Json::Value
claim(
    jtx::Account const& account,
    uint256 const& subscriptionId,
    STAmount const& amount);

/** Set the "StartTime" time tag on a JTx */
class start_time
{
private:
    NetClock::time_point value_;

public:
    explicit start_time(NetClock::time_point const& value) : value_(value)
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

}  // namespace subscription

}  // namespace jtx

}  // namespace test
}  // namespace ripple

#endif
