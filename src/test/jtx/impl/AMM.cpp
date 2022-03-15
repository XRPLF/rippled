//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

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

#include <ripple/app/misc/AMM.h>
#include <ripple/protocol/jss.h>
#include <test/jtx/AMM.h>
#include <test/jtx/Env.h>

namespace ripple {
namespace test {
namespace jtx {

AMM::AMM(
    Env& env,
    Account const& account,
    STAmount const& asset1,
    STAmount const& asset2,
    std::uint8_t weight1,
    std::uint32_t tfee,
    std::optional<ter> const& ter)
    : env_(env)
    , creatorAccount_(account)
    , ammAccountID_(
          ripple::calcAMMAccountID(weight1, asset1.issue(), asset2.issue()))
    , lptIssue_(ripple::calcLPTIssue(ammAccountID_))
    , asset1_(asset1)
    , asset2_(asset2)
    , weight1_(weight1)
    , ter_(ter)
{
    create(tfee);
}

AMM::AMM(
    Env& env,
    Account const& account,
    STAmount const& asset1,
    STAmount const& asset2,
    ter const& ter)
    : AMM(env, account, asset1, asset2, 50, 0, ter)
{
}

void
AMM::create(std::uint32_t tfee)
{
    Json::Value jv;
    jv[jss::Account] = creatorAccount_.human();
    jv[jss::Asset1Details] = asset1_.getJson(JsonOptions::none);
    jv[jss::Asset2Details] = asset2_.getJson(JsonOptions::none);
#if 0
    jv[jss::AssetWeight] = weight1_;
#endif
    jv[jss::TradingFee] = tfee;
    jv[jss::TransactionType] = jss::AMMInstanceCreate;
    if (ter_)
        env_(jv, *ter_);
    else
        env_(jv);
}

std::optional<Json::Value>
AMM::ammInfo(std::optional<Account> const& account) const
{
    Json::Value jv;
    if (account)
        jv[jss::account] = account->human();
    jv[jss::AMMAccount] = to_string(ammAccountID_);
    auto jr = env_.rpc("json", "amm_info", to_string(jv));
    if (jr.isObject() && jr.isMember(jss::result) &&
        jr[jss::result].isMember(jss::status) &&
        jr[jss::result][jss::status].asString() == "success")
        return jr[jss::result];
    return std::nullopt;
}

std::tuple<STAmount, STAmount, STAmount>
AMM::ammBalances(std::optional<AccountID> const& account) const
{
    return getAMMReserves(
        *env_.current(),
        ammAccountID_,
        account,
        std::nullopt,
        std::nullopt,
        env_.journal);
}

bool
AMM::expectBalances(
    STAmount const& asset1,
    std::optional<STAmount> const& asset2,
    std::optional<IOUAmount> const& lpt) const
{
    STAmount a1;
    STAmount a2;
    STAmount l;
    std::tie(a1, a2, l) = ammBalances();
    auto eq = [&](auto const& a) {
        // issue is different
        return a == a1 || a == a2;
    };
    return eq(asset1) && (!asset2 || eq(*asset2)) && (!lpt || lpt == l.iou());
}

bool
AMM::accountRootExists() const
{
    return env_.current()->read(keylet::account(ammAccountID_)) != nullptr;
}

bool
AMM::expectAmmInfo(
    STAmount const& asset1,
    STAmount const& asset2,
    IOUAmount const& balance,
    std::optional<Account> const& account)
{
    auto const jv = ammInfo(account);
    if (!jv || !jv->isMember(jss::Asset1Details) ||
        !jv->isMember(jss::Asset2Details) || !jv->isMember(jss::balance))
        return false;
    auto const& v = jv.value();
    STAmount asset1Details;
    if (!amountFromJsonNoThrow(asset1Details, v[jss::Asset1Details]))
        return false;
    STAmount asset2Details;
    if (!amountFromJsonNoThrow(asset2Details, v[jss::Asset2Details]))
        return false;
    STAmount lptBalance;
    if (!amountFromJsonNoThrow(lptBalance, v[jss::balance]))
        return false;
    // ammInfo returns unordered assets
    if (asset1Details.issue() != asset1.issue())
    {
        auto const tmp = asset1Details;
        asset1Details = asset2Details;
        asset2Details = tmp;
    }
    return asset1 == asset1Details && asset2 == asset2Details &&
        lptBalance == STAmount{balance, lptIssue_};
}

namespace amm {
Json::Value
trust(AccountID const& account, STAmount const& amount, std::uint32_t flags)
{
    if (isXRP(amount))
        Throw<std::runtime_error>("trust() requires IOU");
    Json::Value jv;
    jv[jss::Account] = to_string(account);
    jv[jss::LimitAmount] = amount.getJson(JsonOptions::none);
    jv[jss::TransactionType] = jss::TrustSet;
    jv[jss::Flags] = flags;
    return jv;
}
Json::Value
pay(Account const& account, AccountID const& to, STAmount const& amount)
{
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::Amount] = amount.getJson(JsonOptions::none);
    jv[jss::Destination] = to_string(to);
    jv[jss::TransactionType] = jss::Payment;
    jv[jss::Flags] = tfUniversal;
    return jv;
}
}  // namespace amm
}  // namespace jtx
}  // namespace test
}  // namespace ripple
