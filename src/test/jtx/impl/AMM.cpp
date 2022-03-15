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
    bool log,
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
    , log_(log)
{
    create(tfee);
}

AMM::AMM(
    Env& env,
    Account const& account,
    STAmount const& asset1,
    STAmount const& asset2,
    ter const& ter,
    bool log)
    : AMM(env, account, asset1, asset2, log, 50, 0, ter)
{
}

void
AMM::create(std::uint32_t tfee)
{
    Json::Value jv;
    jv[jss::Account] = creatorAccount_.human();
    jv[jss::Asset1] = asset1_.getJson(JsonOptions::none);
    jv[jss::Asset2] = asset2_.getJson(JsonOptions::none);
#if 0
    jv[jss::AssetWeight] = weight1_;
#endif
    jv[jss::TradingFee] = tfee;
    jv[jss::TransactionType] = jss::AMMInstanceCreate;
    if (log_)
        std::cout << jv.toStyledString();
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
AMM::ammBalances(
    std::optional<AccountID> const& account,
    std::optional<Issue> const& issue1,
    std::optional<Issue> const& issue2) const
{
    return getAMMBalances(
        *env_.current(), ammAccountID_, account, issue1, issue2, env_.journal);
}

bool
AMM::expectBalances(
    STAmount const& asset1,
    std::optional<STAmount> const& asset2,
    std::optional<IOUAmount> const& lpt,
    std::optional<AccountID> const& account) const
{
    STAmount a1;
    STAmount a2;
    STAmount l;
    std::tie(a1, a2, l) = ammBalances(
        account,
        asset1.issue(),
        asset2 ? std::optional<Issue>(asset2->issue()) : std::nullopt);
    return a1 == asset1 && (!asset2 || a2 == *asset2) &&
        (!lpt || *lpt == l.iou());
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
    if (!jv || !jv->isMember(jss::Asset1) || !jv->isMember(jss::Asset2) ||
        !jv->isMember(jss::balance))
        return false;
    auto const& v = jv.value();
    STAmount asset1Info;
    if (!amountFromJsonNoThrow(asset1Info, v[jss::Asset1]))
        return false;
    STAmount asset2Info;
    if (!amountFromJsonNoThrow(asset2Info, v[jss::Asset2]))
        return false;
    STAmount lptBalance;
    if (!amountFromJsonNoThrow(lptBalance, v[jss::balance]))
        return false;
    // ammInfo returns unordered assets
    if (asset1Info.issue() != asset1.issue())
    {
        auto const tmp = asset1Info;
        asset1Info = asset2Info;
        asset2Info = tmp;
    }
    return asset1 == asset1Info && asset2 == asset2Info &&
        lptBalance == STAmount{balance, lptIssue_};
}

void
AMM::deposit(std::optional<Account> const& account, Json::Value& jv)
{
    jv[jss::Account] = account ? account->human() : creatorAccount_.human();
    jv[jss::AMMAccount] = to_string(ammAccountID_);
    jv[jss::TransactionType] = jss::AMMDeposit;
    if (log_)
        std::cout << jv.toStyledString();
    if (ter_)
        env_(jv, *ter_);
    else
        env_(jv);
}

void
AMM::deposit(
    std::optional<Account> const& account,
    std::uint64_t tokens,
    std::optional<STAmount> const& asset1In)
{
    Json::Value jv;
    STAmount saTokens{calcLPTIssue(ammAccountID_), tokens, 0};
    saTokens.setJson(jv[jss::LPTokens]);
    if (asset1In)
        asset1In->setJson(jv[jss::Asset1In]);
    deposit(account, jv);
}

void
AMM::deposit(
    std::optional<Account> const& account,
    STAmount const& asset1In,
    std::optional<STAmount> const& asset2In,
    std::optional<STAmount> const& maxSP)
{
    assert(!(asset2In && maxSP));
    Json::Value jv;
    asset1In.setJson(jv[jss::Asset1In]);
    if (asset2In)
        asset2In->setJson(jv[jss::Asset2In]);
    if (maxSP)
        maxSP->setJson(jv[jss::MaxSP]);
    deposit(account, jv);
}

void
AMM::withdraw(
    std::optional<Account> const& account,
    Json::Value& jv,
    std::optional<ter> const& ter)
{
    jv[jss::Account] = account ? account->human() : creatorAccount_.human();
    jv[jss::AMMAccount] = to_string(ammAccountID_);
    jv[jss::TransactionType] = jss::AMMWithdraw;
    if (log_)
        std::cout << jv.toStyledString();
    if (ter)
        env_(jv, *ter);
    else
        env_(jv);
}

void
AMM::withdraw(
    std::optional<Account> const& account,
    std::uint64_t tokens,
    std::optional<STAmount> const& asset1Out,
    std::optional<ter> const& ter)
{
    Json::Value jv;
    STAmount saTokens{calcLPTIssue(ammAccountID_), tokens, 0};
    saTokens.setJson(jv[jss::LPTokens]);
    if (asset1Out)
        asset1Out->setJson(jv[jss::Asset1Out]);
    withdraw(account, jv, ter);
}

void
AMM::withdraw(
    std::optional<Account> const& account,
    STAmount const& asset1Out,
    std::optional<STAmount> const& asset2Out,
    std::optional<STAmount> const& maxSP,
    std::optional<ter> const& ter)
{
    assert(!(asset2Out && maxSP));
    Json::Value jv;
    asset1Out.setJson(jv[jss::Asset1Out]);
    if (asset2Out)
        asset2Out->setJson(jv[jss::Asset2Out]);
    if (maxSP)
        maxSP->setJson(jv[jss::MaxSP]);
    withdraw(account, jv, ter);
}

void
AMM::swapIn(
    std::optional<Account> const& account,
    STAmount const& assetIn,
    std::optional<std::uint16_t> const& slippage,
    std::optional<STAmount> const& maxSP,
    std::optional<ter> const& ter)
{
    assert(!(maxSP.has_value() && slippage.has_value()));
    Json::Value jv;
    assetIn.setJson(jv[jss::AssetIn]);
    if (slippage)
        jv[jss::Slippage] = *slippage;
    if (maxSP)
        maxSP->setJson(jv[jss::MaxSP]);
    swap(account, jv, ter);
}

void
AMM::swap(
    std::optional<Account> const& account,
    STAmount const& asset,
    std::uint16_t const& slippage,
    STAmount const& maxSP,
    std::optional<ter> const& ter)
{
    Json::Value jv;
    asset.setJson(jv[jss::AssetIn]);
    jv[jss::Slippage] = slippage;
    maxSP.setJson(jv[jss::MaxSP]);
    swap(account, jv, ter);
}

void
AMM::swapOut(
    std::optional<Account> const& account,
    STAmount const& assetOut,
    std::optional<STAmount> const& maxSP,
    std::optional<ter> const& ter)
{
    Json::Value jv;
    assetOut.setJson(jv[jss::AssetOut]);
    if (maxSP)
        maxSP->setJson(jv[jss::MaxSP]);
    swap(account, jv, ter);
}

void
AMM::swap(
    std::optional<Account> const& account,
    Json::Value& jv,
    std::optional<ter> const& ter)
{
    jv[jss::Account] = account ? account->human() : creatorAccount_.human();
    jv[jss::AMMAccount] = to_string(ammAccountID_);
    jv[jss::TransactionType] = jss::AMMSwap;
    if (log_)
        std::cout << jv.toStyledString();
    if (ter)
        env_(jv, *ter);
    else
        env_(jv);
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
