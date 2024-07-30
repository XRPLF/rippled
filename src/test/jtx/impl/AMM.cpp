//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#include <test/jtx/AMM.h>

#include <ripple/app/misc/AMMUtils.h>
#include <ripple/protocol/AMMCore.h>
#include <ripple/protocol/AmountConversions.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/impl/RPCHelpers.h>
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

static IOUAmount
initialTokens(STAmount const& asset1, STAmount const& asset2)
{
    auto const product = number(asset1) * number(asset2);
    return (IOUAmount)(
        product.mantissa() >= 0 ? root2(product) : root2(-product));
}

AMM::AMM(
    Env& env,
    Account const& account,
    STAmount const& asset1,
    STAmount const& asset2,
    bool log,
    std::uint16_t tfee,
    std::uint32_t fee,
    std::optional<std::uint32_t> flags,
    std::optional<jtx::seq> seq,
    std::optional<jtx::msig> ms,
    std::optional<ter> const& ter,
    bool close)
    : env_(env)
    , creatorAccount_(account)
    , asset1_(asset1)
    , asset2_(asset2)
    , ammID_(keylet::amm(asset1_.issue(), asset2_.issue()).key)
    , initialLPTokens_(initialTokens(asset1, asset2))
    , log_(log)
    , doClose_(close)
    , lastPurchasePrice_(0)
    , bidMin_()
    , bidMax_()
    , msig_(ms)
    , fee_(fee)
    , ammAccount_(create(tfee, flags, seq, ter))
    , lptIssue_(ripple::ammLPTIssue(
          asset1_.issue().currency,
          asset2_.issue().currency,
          ammAccount_))
{
}

AMM::AMM(
    Env& env,
    Account const& account,
    STAmount const& asset1,
    STAmount const& asset2,
    ter const& ter,
    bool log,
    bool close)
    : AMM(env,
          account,
          asset1,
          asset2,
          log,
          0,
          0,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          ter,
          close)
{
}

AMM::AMM(
    Env& env,
    Account const& account,
    STAmount const& asset1,
    STAmount const& asset2,
    CreateArg const& arg)
    : AMM(env,
          account,
          asset1,
          asset2,
          arg.log,
          arg.tfee,
          arg.fee,
          arg.flags,
          arg.seq,
          arg.ms,
          arg.err,
          arg.close)
{
}

[[nodiscard]] AccountID
AMM::create(
    std::uint32_t tfee,
    std::optional<std::uint32_t> const& flags,
    std::optional<jtx::seq> const& seq,
    std::optional<ter> const& ter)
{
    Json::Value jv;
    jv[jss::Account] = creatorAccount_.human();
    jv[jss::Amount] = asset1_.getJson(JsonOptions::none);
    jv[jss::Amount2] = asset2_.getJson(JsonOptions::none);
    jv[jss::TradingFee] = tfee;
    jv[jss::TransactionType] = jss::AMMCreate;
    if (flags)
        jv[jss::Flags] = *flags;
    if (fee_ != 0)
        jv[jss::Fee] = std::to_string(fee_);
    else
        jv[jss::Fee] = std::to_string(env_.current()->fees().increment.drops());
    submit(jv, seq, ter);

    if (!ter || env_.ter() == tesSUCCESS)
    {
        if (auto const amm = env_.current()->read(
                keylet::amm(asset1_.issue(), asset2_.issue())))
        {
            return amm->getAccountID(sfAccount);
        }
    }
    return {};
}

Json::Value
AMM::ammRpcInfo(
    std::optional<AccountID> const& account,
    std::optional<std::string> const& ledgerIndex,
    std::optional<Issue> issue1,
    std::optional<Issue> issue2,
    std::optional<AccountID> const& ammAccount,
    bool ignoreParams,
    unsigned apiVersion) const
{
    Json::Value jv;
    if (account)
        jv[jss::account] = to_string(*account);
    if (ledgerIndex)
        jv[jss::ledger_index] = *ledgerIndex;
    if (!ignoreParams)
    {
        if (issue1 || issue2)
        {
            if (issue1)
                jv[jss::asset] =
                    STIssue(sfAsset, *issue1).getJson(JsonOptions::none);
            if (issue2)
                jv[jss::asset2] =
                    STIssue(sfAsset2, *issue2).getJson(JsonOptions::none);
        }
        else if (!ammAccount)
        {
            jv[jss::asset] =
                STIssue(sfAsset, asset1_.issue()).getJson(JsonOptions::none);
            jv[jss::asset2] =
                STIssue(sfAsset2, asset2_.issue()).getJson(JsonOptions::none);
        }
        if (ammAccount)
            jv[jss::amm_account] = to_string(*ammAccount);
    }
    auto jr =
        (apiVersion == RPC::apiInvalidVersion
             ? env_.rpc("json", "amm_info", to_string(jv))
             : env_.rpc(apiVersion, "json", "amm_info", to_string(jv)));
    if (jr.isObject() && jr.isMember(jss::result) &&
        jr[jss::result].isMember(jss::status))
        return jr[jss::result];
    return Json::nullValue;
}

std::tuple<STAmount, STAmount, STAmount>
AMM::balances(
    Issue const& issue1,
    Issue const& issue2,
    std::optional<AccountID> const& account) const
{
    if (auto const amm =
            env_.current()->read(keylet::amm(asset1_.issue(), asset2_.issue())))
    {
        auto const ammAccountID = amm->getAccountID(sfAccount);
        auto const [asset1Balance, asset2Balance] = ammPoolHolds(
            *env_.current(),
            ammAccountID,
            issue1,
            issue2,
            FreezeHandling::fhIGNORE_FREEZE,
            env_.journal);
        auto const lptAMMBalance = account
            ? ammLPHolds(*env_.current(), *amm, *account, env_.journal)
            : amm->getFieldAmount(sfLPTokenBalance);
        return {asset1Balance, asset2Balance, lptAMMBalance};
    }
    return {STAmount{}, STAmount{}, STAmount{}};
}

bool
AMM::expectBalances(
    STAmount const& asset1,
    STAmount const& asset2,
    IOUAmount const& lpt,
    std::optional<AccountID> const& account) const
{
    auto const [asset1Balance, asset2Balance, lptAMMBalance] =
        balances(asset1.issue(), asset2.issue(), account);
    return asset1 == asset1Balance && asset2 == asset2Balance &&
        lptAMMBalance == STAmount{lpt, lptIssue_};
}

IOUAmount
AMM::getLPTokensBalance(std::optional<AccountID> const& account) const
{
    if (account)
        return accountHolds(
                   *env_.current(),
                   *account,
                   lptIssue_,
                   FreezeHandling::fhZERO_IF_FROZEN,
                   env_.journal)
            .iou();
    if (auto const amm =
            env_.current()->read(keylet::amm(asset1_.issue(), asset2_.issue())))
        return amm->getFieldAmount(sfLPTokenBalance).iou();
    return IOUAmount{0};
}

bool
AMM::expectLPTokens(AccountID const& account, IOUAmount const& expTokens) const
{
    if (auto const amm =
            env_.current()->read(keylet::amm(asset1_.issue(), asset2_.issue())))
    {
        auto const lptAMMBalance =
            ammLPHolds(*env_.current(), *amm, account, env_.journal);
        return lptAMMBalance == STAmount{expTokens, lptIssue_};
    }
    return false;
}

bool
AMM::expectAuctionSlot(
    std::uint32_t fee,
    std::optional<std::uint8_t> timeSlot,
    IOUAmount expectedPrice) const
{
    return expectAuctionSlot([&](std::uint32_t slotFee,
                                 std::optional<std::uint8_t> slotInterval,
                                 IOUAmount const& slotPrice,
                                 auto const&) {
        return slotFee == fee &&
            // Auction slot might be expired, in which case slotInterval is
            // 0
            ((!timeSlot && slotInterval == 0) || slotInterval == timeSlot) &&
            slotPrice == expectedPrice;
    });
}

bool
AMM::expectAuctionSlot(std::vector<AccountID> const& authAccounts) const
{
    return expectAuctionSlot([&](std::uint32_t,
                                 std::optional<std::uint8_t>,
                                 IOUAmount const&,
                                 STArray const& accounts) {
        for (auto const& account : accounts)
        {
            if (std::find(
                    authAccounts.cbegin(),
                    authAccounts.cend(),
                    account.getAccountID(sfAccount)) == authAccounts.end())
                return false;
        }
        return true;
    });
}

bool
AMM::expectTradingFee(std::uint16_t fee) const
{
    auto const amm =
        env_.current()->read(keylet::amm(asset1_.issue(), asset2_.issue()));
    return amm && (*amm)[sfTradingFee] == fee;
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
    std::optional<std::string> const& ledger_index,
    std::optional<AccountID> const& ammAccount) const
{
    auto const jv = ammRpcInfo(
        account, ledger_index, std::nullopt, std::nullopt, ammAccount);
    return expectAmmInfo(asset1, asset2, balance, jv);
}

bool
AMM::expectAmmInfo(
    STAmount const& asset1,
    STAmount const& asset2,
    IOUAmount const& balance,
    Json::Value const& jvres) const
{
    if (!jvres.isMember(jss::amm))
        return false;
    auto const& jv = jvres[jss::amm];
    if (!jv.isMember(jss::amount) || !jv.isMember(jss::amount2) ||
        !jv.isMember(jss::lp_token))
        return false;
    STAmount asset1Info;
    if (!amountFromJsonNoThrow(asset1Info, jv[jss::amount]))
        return false;
    STAmount asset2Info;
    if (!amountFromJsonNoThrow(asset2Info, jv[jss::amount2]))
        return false;
    STAmount lptBalance;
    if (!amountFromJsonNoThrow(lptBalance, jv[jss::lp_token]))
        return false;
    // ammRpcInfo returns unordered assets
    if (asset1Info.issue() != asset1.issue())
        std::swap(asset1Info, asset2Info);
    return asset1 == asset1Info && asset2 == asset2Info &&
        lptBalance == STAmount{balance, lptIssue_};
}

void
AMM::setTokens(
    Json::Value& jv,
    std::optional<std::pair<Issue, Issue>> const& assets)
{
    if (assets)
    {
        jv[jss::Asset] =
            STIssue(sfAsset, assets->first).getJson(JsonOptions::none);
        jv[jss::Asset2] =
            STIssue(sfAsset, assets->second).getJson(JsonOptions::none);
    }
    else
    {
        jv[jss::Asset] =
            STIssue(sfAsset, asset1_.issue()).getJson(JsonOptions::none);
        jv[jss::Asset2] =
            STIssue(sfAsset, asset2_.issue()).getJson(JsonOptions::none);
    }
}

IOUAmount
AMM::deposit(
    std::optional<Account> const& account,
    Json::Value& jv,
    std::optional<std::pair<Issue, Issue>> const& assets,
    std::optional<jtx::seq> const& seq,
    std::optional<ter> const& ter)
{
    auto const& acct = account ? *account : creatorAccount_;
    auto const lpTokens = getLPTokensBalance(acct);
    jv[jss::Account] = acct.human();
    setTokens(jv, assets);
    jv[jss::TransactionType] = jss::AMMDeposit;
    if (fee_ != 0)
        jv[jss::Fee] = std::to_string(fee_);
    submit(jv, seq, ter);
    return getLPTokensBalance(acct) - lpTokens;
}

IOUAmount
AMM::deposit(
    std::optional<Account> const& account,
    LPToken tokens,
    std::optional<STAmount> const& asset1In,
    std::optional<std::uint32_t> const& flags,
    std::optional<ter> const& ter)
{
    return deposit(
        account,
        tokens,
        asset1In,
        std::nullopt,
        std::nullopt,
        flags,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        ter);
}

IOUAmount
AMM::deposit(
    std::optional<Account> const& account,
    STAmount const& asset1In,
    std::optional<STAmount> const& asset2In,
    std::optional<STAmount> const& maxEP,
    std::optional<std::uint32_t> const& flags,
    std::optional<ter> const& ter)
{
    assert(!(asset2In && maxEP));
    return deposit(
        account,
        std::nullopt,
        asset1In,
        asset2In,
        maxEP,
        flags,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        ter);
}

IOUAmount
AMM::deposit(
    std::optional<Account> const& account,
    std::optional<LPToken> tokens,
    std::optional<STAmount> const& asset1In,
    std::optional<STAmount> const& asset2In,
    std::optional<STAmount> const& maxEP,
    std::optional<std::uint32_t> const& flags,
    std::optional<std::pair<Issue, Issue>> const& assets,
    std::optional<jtx::seq> const& seq,
    std::optional<std::uint16_t> const& tfee,
    std::optional<ter> const& ter)
{
    Json::Value jv;
    if (tokens)
        tokens->tokens(lptIssue_).setJson(jv[jss::LPTokenOut]);
    if (asset1In)
        asset1In->setJson(jv[jss::Amount]);
    if (asset2In)
        asset2In->setJson(jv[jss::Amount2]);
    if (maxEP)
        maxEP->setJson(jv[jss::EPrice]);
    if (tfee)
        jv[jss::TradingFee] = *tfee;
    std::uint32_t jvflags = 0;
    if (flags)
        jvflags = *flags;
    // If including asset1In and asset2In or tokens as
    // deposit min amounts then must set the flags
    // explicitly instead of relying on this logic.
    if (!(jvflags & tfDepositSubTx))
    {
        if (tokens && !asset1In)
            jvflags |= tfLPToken;
        else if (tokens && asset1In)
            jvflags |= tfOneAssetLPToken;
        else if (asset1In && asset2In)
            jvflags |= tfTwoAsset;
        else if (maxEP && asset1In)
            jvflags |= tfLimitLPToken;
        else if (asset1In)
            jvflags |= tfSingleAsset;
    }
    jv[jss::Flags] = jvflags;
    return deposit(account, jv, assets, seq, ter);
}

IOUAmount
AMM::deposit(DepositArg const& arg)
{
    return deposit(
        arg.account,
        arg.tokens,
        arg.asset1In,
        arg.asset2In,
        arg.maxEP,
        arg.flags,
        arg.assets,
        arg.seq,
        arg.tfee,
        arg.err);
}

IOUAmount
AMM::withdraw(
    std::optional<Account> const& account,
    Json::Value& jv,
    std::optional<jtx::seq> const& seq,
    std::optional<std::pair<Issue, Issue>> const& assets,
    std::optional<ter> const& ter)
{
    auto const& acct = account ? *account : creatorAccount_;
    auto const lpTokens = getLPTokensBalance(acct);
    jv[jss::Account] = acct.human();
    setTokens(jv, assets);
    jv[jss::TransactionType] = jss::AMMWithdraw;
    if (fee_ != 0)
        jv[jss::Fee] = std::to_string(fee_);
    submit(jv, seq, ter);
    return lpTokens - getLPTokensBalance(acct);
}

IOUAmount
AMM::withdraw(
    std::optional<Account> const& account,
    std::optional<LPToken> const& tokens,
    std::optional<STAmount> const& asset1Out,
    std::optional<std::uint32_t> const& flags,
    std::optional<ter> const& ter)
{
    return withdraw(
        account,
        tokens,
        asset1Out,
        std::nullopt,
        std::nullopt,
        flags,
        std::nullopt,
        std::nullopt,
        ter);
}

IOUAmount
AMM::withdraw(
    std::optional<Account> const& account,
    STAmount const& asset1Out,
    std::optional<STAmount> const& asset2Out,
    std::optional<IOUAmount> const& maxEP,
    std::optional<ter> const& ter)
{
    assert(!(asset2Out && maxEP));
    return withdraw(
        account,
        std::nullopt,
        asset1Out,
        asset2Out,
        maxEP,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        ter);
}

IOUAmount
AMM::withdraw(
    std::optional<Account> const& account,
    std::optional<LPToken> const& tokens,
    std::optional<STAmount> const& asset1Out,
    std::optional<STAmount> const& asset2Out,
    std::optional<IOUAmount> const& maxEP,
    std::optional<std::uint32_t> const& flags,
    std::optional<std::pair<Issue, Issue>> const& assets,
    std::optional<jtx::seq> const& seq,
    std::optional<ter> const& ter)
{
    Json::Value jv;
    if (tokens)
        tokens->tokens(lptIssue_).setJson(jv[jss::LPTokenIn]);
    if (asset1Out)
        asset1Out->setJson(jv[jss::Amount]);
    if (asset2Out)
        asset2Out->setJson(jv[jss::Amount2]);
    if (maxEP)
    {
        STAmount const saMaxEP{*maxEP, lptIssue_};
        saMaxEP.setJson(jv[jss::EPrice]);
    }
    std::uint32_t jvflags = 0;
    if (flags)
        jvflags = *flags;
    if (!(jvflags & tfWithdrawSubTx))
    {
        if (tokens && !asset1Out)
            jvflags |= tfLPToken;
        else if (asset1Out && asset2Out)
            jvflags |= tfTwoAsset;
        else if (tokens && asset1Out)
            jvflags |= tfOneAssetLPToken;
        else if (asset1Out && maxEP)
            jvflags |= tfLimitLPToken;
        else if (asset1Out)
            jvflags |= tfSingleAsset;
    }
    jv[jss::Flags] = jvflags;
    return withdraw(account, jv, seq, assets, ter);
}

IOUAmount
AMM::withdraw(WithdrawArg const& arg)
{
    return withdraw(
        arg.account,
        arg.tokens,
        arg.asset1Out,
        arg.asset2Out,
        arg.maxEP,
        arg.flags,
        arg.assets,
        arg.seq,
        arg.err);
}

void
AMM::vote(
    std::optional<Account> const& account,
    std::uint32_t feeVal,
    std::optional<std::uint32_t> const& flags,
    std::optional<jtx::seq> const& seq,
    std::optional<std::pair<Issue, Issue>> const& assets,
    std::optional<ter> const& ter)
{
    Json::Value jv;
    jv[jss::Account] = account ? account->human() : creatorAccount_.human();
    setTokens(jv, assets);
    jv[jss::TradingFee] = feeVal;
    jv[jss::TransactionType] = jss::AMMVote;
    if (flags)
        jv[jss::Flags] = *flags;
    if (fee_ != 0)
        jv[jss::Fee] = std::to_string(fee_);
    submit(jv, seq, ter);
}

void
AMM::vote(VoteArg const& arg)
{
    return vote(arg.account, arg.tfee, arg.flags, arg.seq, arg.assets, arg.err);
}

Json::Value
AMM::bid(BidArg const& arg)
{
    if (auto const amm =
            env_.current()->read(keylet::amm(asset1_.issue(), asset2_.issue())))
    {
        assert(
            !env_.current()->rules().enabled(fixInnerObjTemplate) ||
            amm->isFieldPresent(sfAuctionSlot));
        if (amm->isFieldPresent(sfAuctionSlot))
        {
            auto const& auctionSlot =
                static_cast<STObject const&>(amm->peekAtField(sfAuctionSlot));
            lastPurchasePrice_ = auctionSlot[sfPrice].iou();
        }
    }
    bidMin_ = std::nullopt;
    bidMax_ = std::nullopt;

    Json::Value jv;
    jv[jss::Account] =
        arg.account ? arg.account->human() : creatorAccount_.human();
    setTokens(jv, arg.assets);
    auto getBid = [&](auto const& bid) {
        if (std::holds_alternative<int>(bid))
            return STAmount{lptIssue_, std::get<int>(bid)};
        else if (std::holds_alternative<IOUAmount>(bid))
            return toSTAmount(std::get<IOUAmount>(bid), lptIssue_);
        else
            return std::get<STAmount>(bid);
    };
    if (arg.bidMin)
    {
        STAmount saTokens = getBid(*arg.bidMin);
        saTokens.setJson(jv[jss::BidMin]);
        bidMin_ = saTokens.iou();
    }
    if (arg.bidMax)
    {
        STAmount saTokens = getBid(*arg.bidMax);
        saTokens.setJson(jv[jss::BidMax]);
        bidMax_ = saTokens.iou();
    }
    if (arg.authAccounts.size() > 0)
    {
        Json::Value accounts(Json::arrayValue);
        for (auto const& account : arg.authAccounts)
        {
            Json::Value acct;
            Json::Value authAcct;
            acct[jss::Account] = account.human();
            authAcct[jss::AuthAccount] = acct;
            accounts.append(authAcct);
        }
        jv[jss::AuthAccounts] = accounts;
    }
    if (arg.flags)
        jv[jss::Flags] = *arg.flags;
    jv[jss::TransactionType] = jss::AMMBid;
    if (fee_ != 0)
        jv[jss::Fee] = std::to_string(fee_);
    return jv;
}

void
AMM::submit(
    Json::Value const& jv,
    std::optional<jtx::seq> const& seq,
    std::optional<ter> const& ter)
{
    if (log_)
        std::cout << jv.toStyledString();
    if (msig_)
    {
        if (seq && ter)
            env_(jv, *msig_, *seq, *ter);
        else if (seq)
            env_(jv, *msig_, *seq);
        else if (ter)
            env_(jv, *msig_, *ter);
        else
            env_(jv, *msig_);
    }
    else if (seq && ter)
        env_(jv, *seq, *ter);
    else if (seq)
        env_(jv, *seq);
    else if (ter)
        env_(jv, *ter);
    else
        env_(jv);
    if (doClose_)
        env_.close();
}

bool
AMM::expectAuctionSlot(auto&& cb) const
{
    if (auto const amm =
            env_.current()->read(keylet::amm(asset1_.issue(), asset2_.issue())))
    {
        assert(
            !env_.current()->rules().enabled(fixInnerObjTemplate) ||
            amm->isFieldPresent(sfAuctionSlot));
        if (amm->isFieldPresent(sfAuctionSlot))
        {
            auto const& auctionSlot =
                static_cast<STObject const&>(amm->peekAtField(sfAuctionSlot));
            if (auctionSlot.isFieldPresent(sfAccount))
            {
                // This could fail in pre-fixInnerObjTemplate tests
                // if the submitted transactions recreate one of
                // the failure scenarios. Access as optional
                // to avoid the failure.
                auto const slotFee = auctionSlot[~sfDiscountedFee].value_or(0);
                auto const slotInterval = ammAuctionTimeSlot(
                    env_.app().timeKeeper().now().time_since_epoch().count(),
                    auctionSlot);
                auto const slotPrice = auctionSlot[sfPrice].iou();
                auto const authAccounts =
                    auctionSlot.getFieldArray(sfAuthAccounts);
                return cb(slotFee, slotInterval, slotPrice, authAccounts);
            }
        }
    }
    return false;
}

void
AMM::ammDelete(AccountID const& deleter, std::optional<ter> const& ter)
{
    Json::Value jv;
    jv[jss::Account] = to_string(deleter);
    setTokens(jv);
    jv[jss::TransactionType] = jss::AMMDelete;
    if (fee_ != 0)
        jv[jss::Fee] = std::to_string(fee_);
    submit(jv, std::nullopt, ter);
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
