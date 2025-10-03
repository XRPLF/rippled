//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#include <test/jtx/sponsor.h>
#include <test/jtx/utility.h>

#include <xrpl/protocol/Sign.h>
#include <xrpl/protocol/Sponsor.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {
namespace jtx {

namespace sponsor {

Json::Value
set(jtx::Account const& account,
    uint32_t flags,
    std::optional<uint32_t> reserveCount,
    std::optional<STAmount> feeAmount,
    std::optional<STAmount> maxFee)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::SponsorshipSet;
    jv[jss::Account] = account.human();
    jv[sfFlags.jsonName] = flags;
    if (reserveCount)
        jv[sfReserveCount.jsonName] = *reserveCount;
    if (feeAmount)
        jv[sfFeeAmount.jsonName] = feeAmount->getJson(JsonOptions::none);
    if (maxFee)
        jv[sfMaxFee.jsonName] = maxFee->getJson(JsonOptions::none);
    return jv;
}

Json::Value
set_fee(
    jtx::Account const& account,
    uint32_t flags,
    STAmount feeAmount,
    std::optional<STAmount> maxFee)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::SponsorshipSet;
    jv[jss::Account] = account.human();
    jv[sfFlags.jsonName] = flags;
    jv[sfFeeAmount.jsonName] = feeAmount.getJson(JsonOptions::none);
    if (maxFee)
        jv[sfMaxFee.jsonName] = maxFee->getJson(JsonOptions::none);
    return jv;
}

Json::Value
set_reserve(jtx::Account const& account, uint32_t flags, uint32_t reserveCount)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::SponsorshipSet;
    jv[jss::Account] = account.human();
    jv[sfFlags.jsonName] = flags;
    jv[sfReserveCount.jsonName] = reserveCount;
    return jv;
}

Json::Value
del(jtx::Account const& account)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::SponsorshipSet;
    jv[jss::Account] = account.human();
    jv[sfFlags.jsonName] = tfDeleteObject;
    return jv;
}

Json::Value
transfer(jtx::Account const& account, std::optional<uint256> const& index)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::SponsorshipTransfer;
    jv[jss::Account] = account.human();
    if (index)
        jv[sfObjectID.jsonName] = to_string(*index);
    return jv;
}

void
sponsorAcc::operator()(Env& env, JTx& jt) const
{
    jt.jv[sfSponsorAccount.jsonName] = sponsor_.human();
}

void
sponseeAcc::operator()(Env& env, JTx& jt) const
{
    jt.jv[sfSponsee.jsonName] = sponsee_.human();
}

void
as::operator()(Env& env, JTx& jt) const
{
    jt.jv[sfSponsor.jsonName][sfAccount.jsonName] = sponsor_.human();
    jt.jv[sfSponsor.jsonName][sfFlags.jsonName] = flags;
}

Json::Value
ledgerEntry(
    jtx::Env& env,
    jtx::Account const& sponsor,
    jtx::Account const& sponsee)
{
    Json::Value jvParams;
    jvParams[jss::ledger_index] = jss::validated;
    jvParams[jss::sponsorship][jss::sponsor] = sponsor.human();
    jvParams[jss::sponsorship][jss::sponsee] = sponsee.human();
    return env.rpc("json", "ledger_entry", to_string(jvParams));
}

}  // namespace sponsor
}  // namespace jtx
}  // namespace test
}  // namespace ripple
