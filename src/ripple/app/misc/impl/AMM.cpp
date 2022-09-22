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
#include <ripple/app/paths/TrustLine.h>
#include <ripple/ledger/Sandbox.h>
#include <ripple/protocol/STArray.h>

namespace ripple {

uint256
calcAMMGroupHash(Issue const& issue1, Issue const& issue2)
{
    if (issue1 < issue2)
        return sha512Half(
            issue1.account, issue1.currency, issue2.account, issue2.currency);
    return sha512Half(
        issue2.account, issue2.currency, issue1.account, issue1.currency);
}

Currency
calcLPTCurrency(AccountID const& ammAccountID)
{
    return Currency::fromVoid(ammAccountID.data());
}

Issue
calcLPTIssue(AccountID const& ammAccountID)
{
    return Issue(calcLPTCurrency(ammAccountID), ammAccountID);
}

std::pair<STAmount, STAmount>
ammPoolHolds(
    ReadView const& view,
    AccountID const& ammAccountID,
    Issue const& issue1,
    Issue const& issue2,
    beast::Journal const j)
{
    auto const assetInBalance = accountHolds(
        view,
        ammAccountID,
        issue1.currency,
        issue1.account,
        FreezeHandling::fhZERO_IF_FROZEN,
        j);
    auto const assetOutBalance = accountHolds(
        view,
        ammAccountID,
        issue2.currency,
        issue2.account,
        FreezeHandling::fhZERO_IF_FROZEN,
        j);
    return std::make_pair(assetInBalance, assetOutBalance);
}

std::tuple<STAmount, STAmount, STAmount>
ammHolds(
    ReadView const& view,
    SLE const& ammSle,
    std::optional<Issue> const& optIssue1,
    std::optional<Issue> const& optIssue2,
    beast::Journal const j)
{
    auto const [issue1, issue2] = [&]() -> std::pair<Issue, Issue> {
        if (optIssue1 && optIssue2)
            return {*optIssue1, *optIssue2};
        auto const [issue1, issue2] = getTokensIssue(ammSle);
        if (optIssue1)
        {
            if (*optIssue1 == issue1)
                return {issue1, issue2};
            else if (*optIssue1 == issue2)
                return {issue2, issue1};
            Throw<std::runtime_error>("ammHolds: Invalid optIssue1.");
        }
        else if (optIssue2)
        {
            if (*optIssue2 == issue2)
                return {issue2, issue1};
            else if (*optIssue2 == issue1)
                return {issue1, issue2};
            Throw<std::runtime_error>("ammHolds: Invalid optIssue2.");
        }
        return {issue1, issue2};
    }();
    auto const [asset1, asset2] = ammPoolHolds(
        view, ammSle.getAccountID(sfAMMAccount), issue1, issue2, j);
    return {asset1, asset2, ammSle.getFieldAmount(sfLPTokenBalance)};
}

STAmount
lpHolds(
    ReadView const& view,
    AccountID const& ammAccountID,
    AccountID const& lpAccount,
    beast::Journal const j)
{
    auto const lptIssue = calcLPTIssue(ammAccountID);
    return accountHolds(
        view,
        lpAccount,
        lptIssue.currency,
        lptIssue.account,
        FreezeHandling::fhZERO_IF_FROZEN,
        j);
}

std::optional<TEMcodes>
invalidAmount(std::optional<STAmount> const& a, bool zero)
{
    if (!a)
        return std::nullopt;
    if (badCurrency() == a->getCurrency())
        return temBAD_CURRENCY;
    if (a->native() && a->native() != !a->getIssuer())
        return temBAD_ISSUER;
    if (!zero && *a <= beast::zero)
        return temBAD_AMOUNT;
    return std::nullopt;
}

bool
isFrozen(ReadView const& view, std::optional<STAmount> const& a)
{
    return a && !a->native() && isGlobalFrozen(view, a->getIssuer());
}

std::shared_ptr<STLedgerEntry const>
getAMMSle(ReadView const& view, uint256 ammID)
{
    if (auto const sle = view.read(keylet::amm(ammID));
        (!sle || !view.read(keylet::account(sle->getAccountID(sfAMMAccount)))))
        return nullptr;
    else
        return sle;
}

std::shared_ptr<STLedgerEntry>
getAMMSle(Sandbox& view, uint256 ammID)
{
    if (auto const sle = view.peek(keylet::amm(ammID));
        (!sle || !view.read(keylet::account(sle->getAccountID(sfAMMAccount)))))
        return nullptr;
    else
        return sle;
}

TER
requireAuth(ReadView const& view, Issue const& issue, AccountID const& account)
{
    if (isXRP(issue) || issue.account == account)
        return tesSUCCESS;
    if (auto const issuerAccount = view.read(keylet::account(issue.account));
        issuerAccount && (*issuerAccount)[sfFlags] & lsfRequireAuth)
    {
        if (auto const trustLine =
                view.read(keylet::line(account, issue.account, issue.currency));
            trustLine)
            return !((*trustLine)[sfFlags] &
                     ((account > issue.account) ? lsfLowAuth : lsfHighAuth))
                ? TER{tecNO_AUTH}
                : tesSUCCESS;
        return TER{tecNO_LINE};
    }

    return tesSUCCESS;
}

std::uint16_t
getTradingFee(SLE const& ammSle, AccountID const& account)
{
    if (ammSle.isFieldPresent(sfAuctionSlot))
    {
        auto const& auctionSlot =
            static_cast<STObject const&>(ammSle.peekAtField(sfAuctionSlot));
        if (auctionSlot.isFieldPresent(sfAccount) &&
            auctionSlot.getAccountID(sfAccount) == account)
            return auctionSlot.getFieldU32(sfDiscountedFee);
        if (auctionSlot.isFieldPresent(sfAuthAccounts))
        {
            for (auto const& acct : auctionSlot.getFieldArray(sfAuthAccounts))
                if (acct.getAccountID(sfAccount) == account)
                    return auctionSlot.getFieldU32(sfDiscountedFee);
        }
    }
    return ammSle.getFieldU16(sfTradingFee);
}

std::pair<Issue, Issue>
getTokensIssue(SLE const& ammSle)
{
    auto const ammToken =
        static_cast<STObject const&>(ammSle.peekAtField(sfAMMToken));
    auto getIssue = [&](SField const& field) {
        auto const token =
            static_cast<STObject const&>(ammToken.peekAtField(field));
        Issue issue;
        issue.currency = token.getFieldH160(sfTokenCurrency);
        issue.account = token.getFieldH160(sfTokenIssuer);
        return issue;
    };
    return {getIssue(sfToken1), getIssue(sfToken2)};
}

TER
ammSend(
    ApplyView& view,
    AccountID const& from,
    AccountID const& to,
    STAmount const& amount,
    beast::Journal j)
{
    if (isXRP(amount))
        return accountSend(view, from, to, amount, j);

    auto const issuer = amount.getIssuer();

    if (from == issuer || to == issuer || issuer == noAccount())
    {
        auto const ter = rippleCredit(view, from, to, amount, false, j);
        if (view.rules().enabled(featureDeletableAccounts) && ter != tesSUCCESS)
            return ter;
        return tesSUCCESS;
    }

    TER terResult = rippleCredit(view, issuer, to, amount, true, j);

    if (tesSUCCESS == terResult)
        terResult = rippleCredit(view, from, issuer, amount, true, j);

    return terResult;
}

std::uint16_t
timeSlot(NetClock::time_point const& clock, STObject const& auctionSlot)
{
    using namespace std::chrono;
    std::uint32_t constexpr totalSlotTimeSecs = 24 * 3600;
    std::uint32_t constexpr intervalDuration = totalSlotTimeSecs / 20;
    auto const current =
        duration_cast<seconds>(clock.time_since_epoch()).count();
    if (auctionSlot.isFieldPresent(sfTimeStamp))
    {
        auto const stamp = auctionSlot.getFieldU32(sfTimeStamp);
        auto const diff = current - stamp;
        if (diff < totalSlotTimeSecs)
            return diff / intervalDuration;
    }
    return 0;
}

bool
ammRequiredAmendments(Rules const& rules)
{
    return rules.enabled(featureAMM) && rules.enabled(fixUniversalNumber) &&
        rules.enabled(featureFlowCross);
}

}  // namespace ripple
