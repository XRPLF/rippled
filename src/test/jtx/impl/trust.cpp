//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <test/jtx/trust.h>

#include <xrpl/basics/contract.h>
#include <xrpl/protocol/jss.h>

#include <stdexcept>

namespace ripple {
namespace test {
namespace jtx {

Json::Value
trust(Account const& account, STAmount const& amount, std::uint32_t flags)
{
    if (isXRP(amount))
        Throw<std::runtime_error>("trust() requires IOU");
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::LimitAmount] = amount.getJson(JsonOptions::none);
    jv[jss::TransactionType] = jss::TrustSet;
    jv[jss::Flags] = flags;
    return jv;
}

// This function overload is especially useful for modelling Authorised trust
// lines. account (first function parameter) is the issuing authority, it
// authorises peer (third function parameter) to hold a certain currency
// (amount, the second function parameter)
Json::Value
trust(
    Account const& account,
    STAmount const& amount,
    Account const& peer,
    std::uint32_t flags)
{
    if (isXRP(amount))
        Throw<std::runtime_error>("trust() requires IOU");
    Json::Value jv;
    jv[jss::Account] = account.human();
    {
        auto& ja = jv[jss::LimitAmount] = amount.getJson(JsonOptions::none);
        ja[jss::issuer] = peer.human();
    }
    jv[jss::TransactionType] = jss::TrustSet;
    jv[jss::Flags] = flags;
    return jv;
}

Json::Value
claw(
    Account const& account,
    STAmount const& amount,
    std::optional<Account> const& mptHolder)
{
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::Amount] = amount.getJson(JsonOptions::none);
    jv[jss::TransactionType] = jss::Clawback;

    if (mptHolder)
        jv[sfHolder.jsonName] = mptHolder->human();

    return jv;
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple
