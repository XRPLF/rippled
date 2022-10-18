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
#include <ripple/basics/random.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/impl/GRPCHelpers.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <test/jtx/AMM.h>
#include <test/jtx/Env.h>

namespace ripple {
namespace test {
namespace jtx {

static Number
number(STAmount const& a)
{
    if (isXRP(a))
        return a.xrp();
    return a;
}

AMM::AMM(
    Env& env,
    Account const& account,
    STAmount const& asset1,
    STAmount const& asset2,
    bool log,
    std::uint32_t tfee,
    std::optional<std::uint32_t> flags,
    std::optional<jtx::seq> seq,
    std::optional<ter> const& ter)
    : env_(env)
    , creatorAccount_(account)
    , ammID_(keylet::amm(asset1.issue(), asset2.issue()).key)
    , asset1_(asset1)
    , asset2_(asset2)
    , initialLPTokens_((IOUAmount)root2(number(asset1) * number(asset2)))
    , ter_(ter)
    , log_(log)
{
    create(tfee, flags, seq);
}

AMM::AMM(
    Env& env,
    Account const& account,
    STAmount const& asset1,
    STAmount const& asset2,
    ter const& ter,
    bool log)
    : AMM(env, account, asset1, asset2, log, 0, std::nullopt, std::nullopt, ter)
{
}

void
AMM::create(
    std::uint32_t tfee,
    std::optional<std::uint32_t> flags,
    std::optional<jtx::seq> seq)
{
    Json::Value jv;
    jv[jss::Account] = creatorAccount_.human();
    jv[jss::Asset1] = asset1_.getJson(JsonOptions::none);
    jv[jss::Asset2] = asset2_.getJson(JsonOptions::none);
    jv[jss::TradingFee] = tfee;
    jv[jss::TransactionType] = jss::AMMCreate;
    if (flags)
        jv[jss::Flags] = *flags;
    if (log_)
        std::cout << jv.toStyledString();
    if (ter_ && seq)
        env_(jv, *seq, *ter_);
    else if (ter_)
        env_(jv, *ter_);
    else
        env_(jv);
    env_.close();
    if (!ter_)
    {
        if (auto const amm = env_.current()->read(
                keylet::amm(asset1_.issue(), asset2_.issue())))
        {
            ammAccount_ = amm->getAccountID(sfAMMAccount);
            lptIssue_ = ripple::lptIssue(ammAccount_);
        }
    }
}

std::optional<Json::Value>
AMM::ammRpcInfo(
    std::optional<AccountID> const& account,
    std::optional<std::string> const& ledgerIndex,
    std::optional<uint256> const& ammID,
    bool useAssets) const
{
    Json::Value jv;
    if (account)
        jv[jss::account] = to_string(*account);
    if (ledgerIndex)
        jv[jss::ledger_index] = *ledgerIndex;
    if (useAssets)
    {
        auto setIssue = [&](STAmount const& a,
                            Json::StaticString const& field) {
            if (!a.native())
            {
                a.setJson(jv[field]);
                jv[field].removeMember("value");
            }
            else
            {
                Json::Value v;
                v[jss::currency] = "XRP";
                jv[field] = v;
            }
        };
        setIssue(asset1_, jss::asset1);
        setIssue(asset2_, jss::asset2);
    }
    else if (ammID)
    {
        if (*ammID != uint256(0))
            jv[jss::amm_id] = to_string(*ammID);
    }
    else
        jv[jss::amm_id] = to_string(ammID_);
    auto jr = env_.rpc("json", "amm_info", to_string(jv));
    if (jr.isObject() && jr.isMember(jss::result) &&
        jr[jss::result].isMember(jss::status))
        return jr[jss::result];
    return std::nullopt;
}

bool
AMM::expectBalances(
    STAmount const& asset1,
    STAmount const& asset2,
    IOUAmount const& lpt,
    std::optional<AccountID> const& account,
    std::optional<std::string> const& ledger_index) const
{
    if (auto const amm = env_.current()->read(keylet::amm(ammID_)))
    {
        auto const ammAccountID = amm->getAccountID(sfAMMAccount);
        auto const [asset1Balance, asset2Balance] = ammPoolHolds(
            *env_.current(),
            ammAccountID,
            asset1.issue(),
            asset2.issue(),
            env_.journal);
        auto const lptAMMBalance = account
            ? lpHolds(*env_.current(), ammAccountID, *account, env_.journal)
            : amm->getFieldAmount(sfLPTokenBalance);
        return asset1 == asset1Balance && asset2 == asset2Balance &&
            lptAMMBalance == STAmount{lpt, lptIssue_};
    }
    return false;
}

IOUAmount
AMM::getLPTokensBalance() const
{
    if (auto const amm = env_.current()->read(keylet::amm(ammID_)))
        return amm->getFieldAmount(sfLPTokenBalance).iou();
    return IOUAmount{0};
}

bool
AMM::expectLPTokens(AccountID const& account, IOUAmount const& expTokens) const
{
    if (auto const amm = env_.current()->read(keylet::amm(ammID_)))
    {
        auto const ammAccountID = amm->getAccountID(sfAMMAccount);
        auto const lptAMMBalance =
            lpHolds(*env_.current(), ammAccountID, account, env_.journal);
        return lptAMMBalance == STAmount{expTokens, lptIssue_};
    }
    return false;
}

bool
AMM::expectAuctionSlot(
    std::uint32_t fee,
    std::uint32_t timeInterval,
    IOUAmount const& price,
    std::optional<std::string> const& ledger_index) const
{
    if (auto const amm = env_.current()->read(keylet::amm(ammID_));
        amm && amm->isFieldPresent(sfAuctionSlot))
    {
        auto const& auctionSlot =
            static_cast<STObject const&>(amm->peekAtField(sfAuctionSlot));
        if (auctionSlot.isFieldPresent(sfAccount))
        {
            return auctionSlot.getFieldU32(sfDiscountedFee) == fee &&
                timeSlot(env_.app().timeKeeper().now(), auctionSlot) ==
                timeInterval &&
                auctionSlot.getFieldAmount(sfPrice).iou() == price;
        }
    }
    return false;
}

bool
AMM::expectTradingFee(std::uint16_t fee) const
{
    if (auto const amm = env_.current()->read(keylet::amm(ammID_));
        amm && amm->getFieldU16(sfTradingFee) == fee)
        return true;
    return false;
}

bool
AMM::ammExists() const
{
    return env_.current()->read(keylet::account(ammAccount_)) != nullptr &&
        env_.current()->read(keylet::amm(asset1_.issue(), asset2_.issue())) !=
        nullptr;
}

bool
AMM::expectAmmRpcInfo(
    STAmount const& asset1,
    STAmount const& asset2,
    IOUAmount const& balance,
    std::optional<AccountID> const& account,
    std::optional<std::string> const& ledger_index) const
{
    auto const jv = ammRpcInfo(account, ledger_index);
    if (!jv)
        return false;
    return expectAmmInfo(asset1, asset2, balance, *jv);
}

bool
AMM::expectAmmInfo(
    STAmount const& asset1,
    STAmount const& asset2,
    IOUAmount const& balance,
    Json::Value const& jv) const
{
    if (!jv.isMember(jss::Asset1) || !jv.isMember(jss::Asset2) ||
        !jv.isMember(jss::LPToken))
        return false;
    STAmount asset1Info;
    if (!amountFromJsonNoThrow(asset1Info, jv[jss::Asset1]))
        return false;
    STAmount asset2Info;
    if (!amountFromJsonNoThrow(asset2Info, jv[jss::Asset2]))
        return false;
    STAmount lptBalance;
    if (!amountFromJsonNoThrow(lptBalance, jv[jss::LPToken]))
        return false;
    // ammRpcInfo returns unordered assets
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
AMM::deposit(
    std::optional<Account> const& account,
    Json::Value& jv,
    std::optional<jtx::seq> const& seq)
{
    jv[jss::Account] = account ? account->human() : creatorAccount_.human();
    jv[jss::AMMID] = to_string(ammID_);
    jv[jss::TransactionType] = jss::AMMDeposit;
    if (log_)
        std::cout << jv.toStyledString();
    if (ter_ && seq)
        env_(jv, *seq, *ter_);
    else if (ter_)
        env_(jv, *ter_);
    else
        env_(jv);
    env_.close();
}

void
AMM::deposit(
    std::optional<Account> const& account,
    std::uint64_t tokens,
    std::optional<STAmount> const& asset1In,
    std::optional<std::uint32_t> const& flags,
    std::optional<ter> const& ter)
{
    deposit(
        account,
        tokens,
        asset1In,
        std::nullopt,
        std::nullopt,
        flags,
        std::nullopt,
        ter);
}

void
AMM::deposit(
    std::optional<Account> const& account,
    STAmount const& asset1In,
    std::optional<STAmount> const& asset2In,
    std::optional<STAmount> const& maxEP,
    std::optional<std::uint32_t> const& flags,
    std::optional<ter> const& ter)
{
    assert(!(asset2In && maxEP));
    deposit(
        account,
        std::nullopt,
        asset1In,
        asset2In,
        maxEP,
        flags,
        std::nullopt,
        ter);
}

void
AMM::deposit(
    std::optional<Account> const& account,
    std::optional<std::uint64_t> tokens,
    std::optional<STAmount> const& asset1In,
    std::optional<STAmount> const& asset2In,
    std::optional<STAmount> const& maxEP,
    std::optional<std::uint32_t> const& flags,
    std::optional<jtx::seq> const& seq,
    std::optional<ter> const& ter)
{
    if (ter)
        ter_ = *ter;
    Json::Value jv;
    if (tokens)
    {
        STAmount saTokens{lptIssue_, *tokens, 0};
        saTokens.setJson(jv[jss::LPTokenOut]);
    }
    if (asset1In)
        asset1In->setJson(jv[jss::Asset1In]);
    if (asset2In)
        asset2In->setJson(jv[jss::Asset2In]);
    if (maxEP)
        maxEP->setJson(jv[jss::EPrice]);
    if (flags)
        jv[jss::Flags] = *flags;
    deposit(account, jv, seq);
}

void
AMM::withdraw(
    std::optional<Account> const& account,
    Json::Value& jv,
    std::optional<jtx::seq> const& seq,
    std::optional<ter> const& ter)
{
    jv[jss::Account] = account ? account->human() : creatorAccount_.human();
    jv[jss::AMMID] = to_string(ammID_);
    jv[jss::TransactionType] = jss::AMMWithdraw;
    if (log_)
        std::cout << jv.toStyledString();
    if (ter && seq)
        env_(jv, *seq, *ter);
    else if (ter)
        env_(jv, *ter);
    else
        env_(jv);
    env_.close();
}

void
AMM::withdraw(
    std::optional<Account> const& account,
    std::optional<std::uint64_t> const& tokens,
    std::optional<STAmount> const& asset1Out,
    std::optional<std::uint32_t> const& flags,
    std::optional<ter> const& ter)
{
    withdraw(
        account,
        tokens,
        asset1Out,
        std::nullopt,
        std::nullopt,
        flags,
        std::nullopt,
        ter);
}

void
AMM::withdraw(
    std::optional<Account> const& account,
    STAmount const& asset1Out,
    std::optional<STAmount> const& asset2Out,
    std::optional<IOUAmount> const& maxEP,
    std::optional<ter> const& ter)
{
    assert(!(asset2Out && maxEP));
    withdraw(
        account,
        std::nullopt,
        asset1Out,
        asset2Out,
        maxEP,
        std::nullopt,
        std::nullopt,
        ter);
}

void
AMM::withdraw(
    std::optional<Account> const& account,
    std::optional<std::uint64_t> const& tokens,
    std::optional<STAmount> const& asset1Out,
    std::optional<STAmount> const& asset2Out,
    std::optional<IOUAmount> const& maxEP,
    std::optional<std::uint32_t> const& flags,
    std::optional<jtx::seq> const& seq,
    std::optional<ter> const& ter)
{
    Json::Value jv;
    if (tokens)
    {
        STAmount saTokens{lptIssue_, *tokens, 0};
        saTokens.setJson(jv[jss::LPTokenIn]);
    }
    if (asset1Out)
        asset1Out->setJson(jv[jss::Asset1Out]);
    if (asset2Out)
        asset2Out->setJson(jv[jss::Asset2Out]);
    if (maxEP)
    {
        STAmount const saMaxEP{*maxEP, lptIssue_};
        saMaxEP.setJson(jv[jss::EPrice]);
    }
    if (flags)
        jv[jss::Flags] = *flags;
    withdraw(account, jv, seq, ter);
}

void
AMM::vote(
    std::optional<Account> const& account,
    std::uint32_t feeVal,
    std::optional<std::uint32_t> const& flags,
    std::optional<jtx::seq> const& seq,
    std::optional<ter> const& ter)
{
    Json::Value jv;
    jv[jss::Account] = account ? account->human() : creatorAccount_.human();
    jv[jss::AMMID] = to_string(ammID_);
    jv[jss::TradingFee] = feeVal;
    jv[jss::TransactionType] = jss::AMMVote;
    if (flags)
        jv[jss::Flags] = *flags;
    if (log_)
        std::cout << jv.toStyledString();
    if (ter && seq)
        env_(jv, *seq, *ter);
    else if (ter)
        env_(jv, *ter);
    else
        env_(jv);
    env_.close();
}

void
AMM::bid(
    std::optional<Account> const& account,
    std::optional<std::uint64_t> const& minSlotPrice,
    std::optional<std::uint64_t> const& maxSlotPrice,
    std::vector<Account> const& authAccounts,
    std::optional<std::uint32_t> const& flags,
    std::optional<jtx::seq> const& seq,
    std::optional<ter> const& ter)
{
    Json::Value jv;
    jv[jss::Account] = account ? account->human() : creatorAccount_.human();
    jv[jss::AMMID] = to_string(ammID_);
    if (minSlotPrice)
    {
        STAmount saTokens{lptIssue_, *minSlotPrice, 0};
        saTokens.setJson(jv[jss::MinSlotPrice]);
    }
    if (maxSlotPrice)
    {
        STAmount saTokens{lptIssue_, *maxSlotPrice, 0};
        saTokens.setJson(jv[jss::MaxSlotPrice]);
    }
    if (authAccounts.size() > 0)
    {
        Json::Value accounts(Json::arrayValue);
        for (auto const& account : authAccounts)
        {
            Json::Value acct;
            Json::Value authAcct;
            acct[jss::Account] = account.human();
            authAcct[jss::AuthAccount] = acct;
            accounts.append(authAcct);
        }
        jv[jss::AuthAccounts] = accounts;
    }
    if (flags)
        jv[jss::Flags] = *flags;
    jv[jss::TransactionType] = jss::AMMBid;
    if (log_)
        std::cout << jv.toStyledString();
    if (ter && seq)
        env_(jv, *seq, *ter);
    else if (ter)
        env_(jv, *ter);
    else
        env_(jv);
    env_.close();
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
