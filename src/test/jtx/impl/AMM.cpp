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
    , ammHash_(ripple::calcAMMGroupHash(asset1.issue(), asset2.issue()))
    , ammAccount_{to_string(ripple::rand_int())}
    , asset1_(asset1)
    , asset2_(asset2)
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
    jv[jss::AMMAccount] = ammAccount_.human();
    jv[jss::Account] = creatorAccount_.human();
    jv[jss::Asset1] = asset1_.getJson(JsonOptions::none);
    jv[jss::Asset2] = asset2_.getJson(JsonOptions::none);
    jv[jss::TradingFee] = tfee;
    jv[jss::TransactionType] = jss::AMMInstanceCreate;
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
    lptIssue_ = ripple::calcLPTIssue(ammAccount_.id());
}

std::optional<Json::Value>
AMM::ammRpcInfo(
    std::optional<AccountID> const& account,
    std::optional<std::string> const& ledgerIndex,
    std::optional<uint256> const& ammHash,
    bool useAssets) const
{
    Json::Value jv;
    if (account)
        jv[jss::account] = to_string(*account);
    if (ledgerIndex)
        jv[jss::ledger_index] = *ledgerIndex;
    if (useAssets)
    {
        asset1_.setJson(jv[jss::Asset1]);
        asset2_.setJson(jv[jss::Asset2]);
    }
    else if (ammHash)
    {
        if (*ammHash != uint256(0))
            jv[jss::AMMHash] = to_string(*ammHash);
    }
    else
        jv[jss::AMMHash] = to_string(ammHash_);
    auto jr = env_.rpc("json", "amm_info", to_string(jv));
    if (jr.isObject() && jr.isMember(jss::result) &&
        jr[jss::result].isMember(jss::status))
        return jr[jss::result];
    return std::nullopt;
}

std::optional<Json::Value>
AMM::ammgRPCInfo(
    const std::optional<AccountID>& account,
    const std::optional<std::string>& ledgerIndex,
    std::optional<uint256> const& ammHash,
    bool useAssets) const
{
    auto config = env_.app().config();
    auto const grpcPort = config["port_grpc"].get<std::string>("port");
    if (!grpcPort.has_value())
        return {};
    AMMgRPCInfoClient client(*grpcPort);
    if (useAssets)
    {
        RPC::convert(*client.request.mutable_asset1(), asset1_);
        RPC::convert(*client.request.mutable_asset2(), asset2_);
    }
    else if (ammHash)
    {
        if (ammHash != uint256{0})
            *client.request.mutable_ammhash()->mutable_value() =
                to_string(*ammHash);
    }
    else
        *client.request.mutable_ammhash()->mutable_value() =
            to_string(ammHash_);
    if (account)
        *client.request.mutable_account()->mutable_value()->mutable_address() =
            to_string(*account);
    if (ledgerIndex)
        *client.request.mutable_ledger()->mutable_hash() = *ledgerIndex;
    auto const status = client.getAMMInfo();
    Json::Value jv;
    if (!status.ok())
    {
        jv[jss::error_message] = status.error_message();
        jv[jss::error_code] = status.error_code();
        jv[jss::error] = status.error_details();
        return jv;
    }
    auto const ammAccountID =
        parseBase58<AccountID>(client.reply.ammaccount().value().address());
    auto getAmt = [&](auto const& amt) {
        if (amt.value().has_xrp_amount())
            return STAmount{xrpIssue(), amt.value().xrp_amount().drops(), 0};
        auto const iou = amt.value().issued_currency_amount();
        auto const account =
            RPC::accountFromStringStrict(iou.issuer().address());
        Currency currency = to_currency(iou.currency().name());
        auto const value = iou.value();
        if (!account.has_value() || currency == badCurrency())
            return STAmount{};
        return amountFromString(Issue(currency, *account), value);
    };
    getAmt(client.reply.asset1()).setJson(jv[jss::Asset1]);
    getAmt(client.reply.asset2()).setJson(jv[jss::Asset2]);
    getAmt(client.reply.tokens()).setJson(jv[jss::LPTokens]);
    jv[jss::AMMAccount] = ammAccountID ? to_string(*ammAccountID) : "";
    jv[jss::AMMHash] = client.reply.ammhash().value();
    return jv;
}

bool
AMM::expectBalances(
    STAmount const& asset1,
    STAmount const& asset2,
    IOUAmount const& lpt,
    std::optional<AccountID> const& account,
    std::optional<std::string> const& ledger_index) const
{
    return expectAmmRpcInfo(asset1, asset2, lpt, account, ledger_index);
}

bool
AMM::expectLPTokens(AccountID const& account, IOUAmount const& expTokens) const
{
    try
    {
        STAmount saTokens;
        if (auto const jv = *ammRpcInfo(account);
            amountFromJsonNoThrow(saTokens, jv[jss::LPTokens]))
            return saTokens.iou() == expTokens;
    }
    catch (...)
    {
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
    try
    {
        if (auto const jv = *ammRpcInfo(std::nullopt, ledger_index))
        {
            auto const auctionSlot = jv[jss::AuctionSlot];
            STAmount saPrice;
            return auctionSlot[jss::DiscountedFee].asUInt() == fee &&
                auctionSlot[jss::TimeInterval].asUInt() == timeInterval &&
                amountFromJsonNoThrow(saPrice, auctionSlot[jss::Price]) &&
                saPrice.iou() == price;
        }
    }
    catch (...)
    {
    }
    return false;
}

bool
AMM::ammExists() const
{
    return env_.current()->read(keylet::account(ammAccount_.id())) != nullptr &&
        env_.current()->read(keylet::amm(
            calcAMMGroupHash(asset1_.issue(), asset2_.issue()))) != nullptr;
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
AMM::expectAmmgRPCInfo(
    STAmount const& asset1,
    STAmount const& asset2,
    IOUAmount const& balance,
    std::optional<AccountID> const& account) const
{
    auto const jv = ammgRPCInfo(account);
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
        !jv.isMember(jss::LPTokens))
        return false;
    STAmount asset1Info;
    if (!amountFromJsonNoThrow(asset1Info, jv[jss::Asset1]))
        return false;
    STAmount asset2Info;
    if (!amountFromJsonNoThrow(asset2Info, jv[jss::Asset2]))
        return false;
    STAmount lptBalance;
    if (!amountFromJsonNoThrow(lptBalance, jv[jss::LPTokens]))
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
AMM::deposit(std::optional<Account> const& account, Json::Value& jv)
{
    jv[jss::Account] = account ? account->human() : creatorAccount_.human();
    jv[jss::AMMHash] = to_string(ammHash_);
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
    std::optional<STAmount> const& asset1In,
    std::optional<ter> const& ter)
{
    ter_ = ter;
    Json::Value jv;
    STAmount saTokens{calcLPTIssue(ammAccount_.id()), tokens, 0};
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
    std::optional<STAmount> const& maxEP,
    std::optional<ter> const& ter)
{
    assert(!(asset2In && maxEP));
    ter_ = ter;
    Json::Value jv;
    asset1In.setJson(jv[jss::Asset1In]);
    if (asset2In)
        asset2In->setJson(jv[jss::Asset2In]);
    if (maxEP)
        maxEP->setJson(jv[jss::EPrice]);
    deposit(account, jv);
}

void
AMM::withdraw(
    std::optional<Account> const& account,
    Json::Value& jv,
    std::optional<ter> const& ter)
{
    jv[jss::Account] = account ? account->human() : creatorAccount_.human();
    jv[jss::AMMHash] = to_string(ammHash_);
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
    if (tokens == 0)
    {
        jv[jss::Flags] = tfAMMWithdrawAll;
    }
    else
    {
        STAmount saTokens{calcLPTIssue(ammAccount_.id()), tokens, 0};
        saTokens.setJson(jv[jss::LPTokens]);
    }
    if (asset1Out)
        asset1Out->setJson(jv[jss::Asset1Out]);
    withdraw(account, jv, ter);
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
    Json::Value jv;
    asset1Out.setJson(jv[jss::Asset1Out]);
    if (asset2Out)
        asset2Out->setJson(jv[jss::Asset2Out]);
    if (maxEP)
    {
        STAmount const saMaxEP{*maxEP, lptIssue_};
        saMaxEP.setJson(jv[jss::EPrice]);
    }
    withdraw(account, jv, ter);
}

void
AMM::vote(
    std::optional<Account> const& account,
    std::uint32_t feeVal,
    std::optional<ter> const& ter)
{
    Json::Value jv;
    jv[jss::Account] = account ? account->human() : creatorAccount_.human();
    jv[jss::AMMHash] = to_string(ammHash_);
    jv[jss::FeeVal] = feeVal;
    jv[jss::TransactionType] = jss::AMMVote;
    if (log_)
        std::cout << jv.toStyledString();
    if (ter)
        env_(jv, *ter);
    else
        env_(jv);
}

void
AMM::bid(
    std::optional<Account> const& account,
    std::optional<std::uint64_t> const& minSlotPrice,
    std::optional<std::uint64_t> const& maxSlotPrice,
    std::vector<Account> const& authAccounts,
    std::optional<ter> const& ter)
{
    Json::Value jv;
    jv[jss::Account] = account ? account->human() : creatorAccount_.human();
    jv[jss::AMMHash] = to_string(ammHash_);
    if (minSlotPrice)
    {
        STAmount saTokens{calcLPTIssue(ammAccount_.id()), *minSlotPrice, 0};
        saTokens.setJson(jv[jss::MinSlotPrice]);
    }
    if (maxSlotPrice)
    {
        STAmount saTokens{calcLPTIssue(ammAccount_.id()), *maxSlotPrice, 0};
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
    jv[jss::TransactionType] = jss::AMMBid;
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
