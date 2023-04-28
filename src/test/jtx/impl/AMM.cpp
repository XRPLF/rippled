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

#include <ripple/app/misc/AMMUtils.h>
#include <ripple/protocol/AMMCore.h>
#include <ripple/protocol/AmountConversions.h>
#include <ripple/protocol/jss.h>
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
    std::uint32_t tfee,
    std::uint32_t fee,
    std::optional<std::uint32_t> flags,
    std::optional<jtx::seq> seq,
    std::optional<jtx::msig> ms,
    std::optional<ter> const& ter)
    : env_(env)
    , creatorAccount_(account)
    , asset1_(asset1)
    , asset2_(asset2)
    , initialLPTokens_(initialTokens(asset1, asset2))
    , ter_(ter)
    , log_(log)
    , lastPurchasePrice_(0)
    , minSlotPrice_(0)
    , bidMin_()
    , bidMax_()
    , msig_(ms)
    , fee_(fee)
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
          ter)
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
    submit(jv, seq, ter_);

    if (!ter_ || env_.ter() == tesSUCCESS)
    {
        if (auto const amm = env_.current()->read(
                keylet::amm(asset1_.issue(), asset2_.issue())))
        {
            ammAccount_ = amm->getAccountID(sfAccount);
            lptIssue_ = ripple::ammLPTIssue(
                asset1_.issue().currency,
                asset2_.issue().currency,
                ammAccount_);
        }
    }
}

std::optional<Json::Value>
AMM::ammRpcInfo(
    std::optional<AccountID> const& account,
    std::optional<std::string> const& ledgerIndex,
    std::optional<std::pair<Issue, Issue>> tokens) const
{
    Json::Value jv;
    if (account)
        jv[jss::account] = to_string(*account);
    if (ledgerIndex)
        jv[jss::ledger_index] = *ledgerIndex;
    if (tokens)
    {
        jv[jss::asset] =
            STIssue(sfAsset, tokens->first).getJson(JsonOptions::none);
        jv[jss::asset2] =
            STIssue(sfAsset2, tokens->first).getJson(JsonOptions::none);
    }
    else
    {
        jv[jss::asset] =
            STIssue(sfAsset, asset1_.issue()).getJson(JsonOptions::none);
        jv[jss::asset2] =
            STIssue(sfAsset2, asset2_.issue()).getJson(JsonOptions::none);
    }
    auto jr = env_.rpc("json", "amm_info", to_string(jv));
    if (jr.isObject() && jr.isMember(jss::result) &&
        jr[jss::result].isMember(jss::status))
        return jr[jss::result];
    return std::nullopt;
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
    std::optional<AccountID> const& account,
    std::optional<std::string> const& ledger_index) const
{
    auto const [asset1Balance, asset2Balance, lptAMMBalance] =
        balances(asset1.issue(), asset2.issue(), account);
    return asset1 == asset1Balance && asset2 == asset2Balance &&
        lptAMMBalance == STAmount{lpt, lptIssue_};
    return false;
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
    std::optional<std::uint8_t> purchasedTimeSlot,
    std::optional<std::string> const& ledger_index) const
{
    return expectAuctionSlot([&](std::uint32_t slotFee,
                                 std::optional<std::uint8_t> slotInterval,
                                 IOUAmount const& slotPrice,
                                 auto const&) {
        if (!purchasedTimeSlot)
            purchasedTimeSlot = timeSlot;

        auto const lastPurchasePrice =
            !purchasedTimeSlot ? IOUAmount{0} : lastPurchasePrice_;
        auto const expectedPrice =
            expectedPurchasePrice(purchasedTimeSlot, lastPurchasePrice);
        return slotFee == fee &&
            // Auction slot might be expired, in which case slotInterval is
            // 0
            ((!timeSlot && slotInterval == 0) || slotInterval == timeSlot) &&
            slotPrice == expectedPrice;
    });
}

bool
AMM::expectAuctionSlot(
    std::uint32_t fee,
    std::optional<std::uint8_t> timeSlot,
    IOUAmount expectedPrice,
    std::optional<std::string> const& ledger_index) const
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
    if (auto const amm =
            env_.current()->read(keylet::amm(asset1_.issue(), asset2_.issue()));
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
    Json::Value const& jvres) const
{
    if (!jvres.isMember(jss::amm))
        return false;
    auto const jv = jvres[jss::amm];
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
    {
        auto const tmp = asset1Info;
        asset1Info = asset2Info;
        asset2Info = tmp;
    }
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

void
AMM::deposit(
    std::optional<Account> const& account,
    Json::Value& jv,
    std::optional<std::pair<Issue, Issue>> const& assets,
    std::optional<jtx::seq> const& seq)
{
    jv[jss::Account] = account ? account->human() : creatorAccount_.human();
    setTokens(jv, assets);
    jv[jss::TransactionType] = jss::AMMDeposit;
    if (fee_ != 0)
        jv[jss::Fee] = std::to_string(fee_);
    submit(jv, seq, ter_);
}

void
AMM::deposit(
    std::optional<Account> const& account,
    LPToken tokens,
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
        std::nullopt,
        ter);
}

void
AMM::deposit(
    std::optional<Account> const& account,
    std::optional<LPToken> tokens,
    std::optional<STAmount> const& asset1In,
    std::optional<STAmount> const& asset2In,
    std::optional<STAmount> const& maxEP,
    std::optional<std::uint32_t> const& flags,
    std::optional<std::pair<Issue, Issue>> const& assets,
    std::optional<jtx::seq> const& seq,
    std::optional<ter> const& ter)
{
    if (ter)
        ter_ = *ter;
    Json::Value jv;
    if (tokens)
        tokens->tokens(lptIssue_).setJson(jv[jss::LPTokenOut]);
    if (asset1In)
        asset1In->setJson(jv[jss::Amount]);
    if (asset2In)
        asset2In->setJson(jv[jss::Amount2]);
    if (maxEP)
        maxEP->setJson(jv[jss::EPrice]);
    std::uint32_t jvflags = 0;
    if (flags)
        jvflags = *flags;
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
    deposit(account, jv, assets, seq);
    if (ter)
        ter_ = std::nullopt;
}

void
AMM::withdraw(
    std::optional<Account> const& account,
    Json::Value& jv,
    std::optional<jtx::seq> const& seq,
    std::optional<std::pair<Issue, Issue>> const& assets,
    std::optional<ter> const& ter)
{
    jv[jss::Account] = account ? account->human() : creatorAccount_.human();
    setTokens(jv, assets);
    jv[jss::TransactionType] = jss::AMMWithdraw;
    if (fee_ != 0)
        jv[jss::Fee] = std::to_string(fee_);
    submit(jv, seq, ter);
}

void
AMM::withdraw(
    std::optional<Account> const& account,
    std::optional<LPToken> const& tokens,
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
        std::nullopt,
        ter);
}

void
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
    withdraw(account, jv, seq, assets, ter);
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
AMM::bid(
    std::optional<Account> const& account,
    std::optional<std::variant<int, IOUAmount, STAmount>> const& bidMin,
    std::optional<std::variant<int, IOUAmount, STAmount>> const& bidMax,
    std::vector<Account> const& authAccounts,
    std::optional<std::uint32_t> const& flags,
    std::optional<jtx::seq> const& seq,
    std::optional<std::pair<Issue, Issue>> const& assets,
    std::optional<ter> const& ter)
{
    if (auto const amm =
            env_.current()->read(keylet::amm(asset1_.issue(), asset2_.issue())))
    {
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
    jv[jss::Account] = account ? account->human() : creatorAccount_.human();
    setTokens(jv, assets);
    auto getBid = [&](auto const& bid) {
        if (std::holds_alternative<int>(bid))
            return STAmount{lptIssue_, std::get<int>(bid)};
        else if (std::holds_alternative<IOUAmount>(bid))
            return toSTAmount(std::get<IOUAmount>(bid), lptIssue_);
        else
            return std::get<STAmount>(bid);
    };
    if (bidMin)
    {
        STAmount saTokens = getBid(*bidMin);
        saTokens.setJson(jv[jss::BidMin]);
        bidMin_ = saTokens.iou();
    }
    if (bidMax)
    {
        STAmount saTokens = getBid(*bidMax);
        saTokens.setJson(jv[jss::BidMax]);
        bidMax_ = saTokens.iou();
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
    if (fee_ != 0)
        jv[jss::Fee] = std::to_string(fee_);
    submit(jv, seq, ter);
}

IOUAmount
AMM::expectedPurchasePrice(
    std::optional<std::uint8_t> timeSlot,
    IOUAmount const& lastPurchasePrice) const
{
    auto const p1_05 = Number(105, -2);
    std::uint32_t constexpr nIntervals = 20;

    if (!timeSlot)
    {
        if (bidMin_ && !bidMax_)
            return *bidMin_;
        return IOUAmount(0);
    }

    auto const computedPrice = [&]() {
        if (timeSlot == 0)
            return IOUAmount(lastPurchasePrice * p1_05 + minSlotPrice_);

        auto const fractionUsed = (Number(*timeSlot) + 1) / nIntervals;
        return IOUAmount(
            lastPurchasePrice * p1_05 * (1 - power(fractionUsed, 60)) +
            minSlotPrice_);
    }();

    // assume price is in range
    if (bidMin_ && !bidMax_)
        return std::max(computedPrice, *bidMin_);
    return computedPrice;
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
    env_.close();
}

bool
AMM::expectAuctionSlot(auto&& cb) const
{
    if (auto const amm =
            env_.current()->read(keylet::amm(asset1_.issue(), asset2_.issue()));
        amm && amm->isFieldPresent(sfAuctionSlot))
    {
        auto const& auctionSlot =
            static_cast<STObject const&>(amm->peekAtField(sfAuctionSlot));
        if (auctionSlot.isFieldPresent(sfAccount))
        {
            auto const slotFee = auctionSlot.getFieldU32(sfDiscountedFee);
            auto const slotInterval = ammAuctionTimeSlot(
                env_.app().timeKeeper().now().time_since_epoch().count(),
                auctionSlot);
            auto const slotPrice = auctionSlot[sfPrice].iou();
            auto const authAccounts = auctionSlot.getFieldArray(sfAuthAccounts);
            return cb(slotFee, slotInterval, slotPrice, authAccounts);
        }
    }
    return false;
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
