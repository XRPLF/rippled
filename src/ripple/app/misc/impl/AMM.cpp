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
#include <ripple/basics/Log.h>
#include <ripple/ledger/Sandbox.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/STObject.h>

namespace ripple {

AccountID
ammAccountID(uint256 const& parentHash, uint256 const& ammID)
{
    ripesha_hasher rsh;
    auto hash = sha512Half(parentHash, ammID);
    rsh(hash.data(), hash.size());
    return AccountID{static_cast<ripesha_hasher::result_type>(rsh)};
}

uint256
ammIndex(Issue const& issue1, Issue const& issue2)
{
    auto const& [minI, maxI] = std::minmax(issue1, issue2);
    return sha512Half(minI.account, minI.currency, maxI.account, maxI.currency);
}

Currency
lptCurrency(AccountID const& ammAccountID)
{
    return Currency::fromVoid(ammAccountID.data());
}

Issue
lptIssue(AccountID const& ammAccountID)
{
    return Issue(lptCurrency(ammAccountID), ammAccountID);
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
        view, ammAccountID, issue1, FreezeHandling::fhZERO_IF_FROZEN, j);
    auto const assetOutBalance = accountHolds(
        view, ammAccountID, issue2, FreezeHandling::fhZERO_IF_FROZEN, j);
    return std::make_pair(assetInBalance, assetOutBalance);
}

Expected<std::tuple<STAmount, STAmount, STAmount>, TER>
ammHolds(
    ReadView const& view,
    SLE const& ammSle,
    std::optional<Issue> const& optIssue1,
    std::optional<Issue> const& optIssue2,
    beast::Journal const j)
{
    auto const issues = [&]() -> std::optional<std::pair<Issue, Issue>> {
        if (optIssue1 && optIssue2)
            return {{*optIssue1, *optIssue2}};
        auto const [issue1, issue2] = getTokensIssue(ammSle);
        if (optIssue1)
        {
            if (*optIssue1 == issue1)
                return {{issue1, issue2}};
            else if (*optIssue1 == issue2)
                return {{issue2, issue1}};
            JLOG(j.debug()) << "ammHolds: Invalid optIssue1 " << *optIssue1;
            return std::nullopt;
        }
        else if (optIssue2)
        {
            if (*optIssue2 == issue2)
                return {{issue2, issue1}};
            else if (*optIssue2 == issue1)
                return {{issue1, issue2}};
            JLOG(j.debug()) << "ammHolds: Invalid optIssue2 " << *optIssue2;
            return std::nullopt;
        }
        return {{issue1, issue2}};
    }();
    if (!issues)
        return Unexpected(tecAMM_INVALID_TOKENS);
    auto const [asset1, asset2] = ammPoolHolds(
        view,
        ammSle.getAccountID(sfAMMAccount),
        issues->first,
        issues->second,
        j);
    return std::make_tuple(asset1, asset2, ammSle[sfLPTokenBalance]);
}

STAmount
lpHolds(
    ReadView const& view,
    AccountID const& ammAccountID,
    AccountID const& lpAccount,
    beast::Journal const j)
{
    auto const lptIss = lptIssue(ammAccountID);
    return accountHolds(
        view,
        lpAccount,
        lptIss.currency,
        lptIss.account,
        FreezeHandling::fhZERO_IF_FROZEN,
        j);
}

NotTEC
invalidAMMAmount(std::optional<STAmount> const& a, bool nonNegative)
{
    if (!a)
        return tesSUCCESS;
    if (badCurrency() == a->getCurrency())
        return temBAD_CURRENCY;
    if (a->native() && a->native() != !a->getIssuer())
        return temBAD_ISSUER;
    if (!nonNegative && *a <= beast::zero)
        return temBAD_AMOUNT;
    return tesSUCCESS;
}

bool
isFrozen(ReadView const& view, std::optional<STAmount> const& a)
{
    return a && !a->native() && isGlobalFrozen(view, a->getIssuer());
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
getTradingFee(ReadView const& view, SLE const& ammSle, AccountID const& account)
{
    using namespace std::chrono;
    if (ammSle.isFieldPresent(sfAuctionSlot))
    {
        auto const& auctionSlot =
            static_cast<STObject const&>(ammSle.peekAtField(sfAuctionSlot));
        if (auto const timeStamp = auctionSlot[~sfTimeStamp])
        {
            std::uint32_t constexpr totalSlotTimeSecs = 24 * 3600;
            auto const notExpired =
                (duration_cast<seconds>(
                     view.info().parentCloseTime.time_since_epoch())
                     .count() -
                 *timeStamp) <= totalSlotTimeSecs;
            if (auctionSlot[~sfAccount] == account && notExpired)
                return auctionSlot[sfDiscountedFee];
            if (auctionSlot.isFieldPresent(sfAuthAccounts))
            {
                for (auto const& acct :
                     auctionSlot.getFieldArray(sfAuthAccounts))
                    if (acct[~sfAccount] == account && notExpired)
                        return auctionSlot[sfDiscountedFee];
            }
        }
    }
    return ammSle[sfTradingFee];
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
        issue.currency = token[sfTokenCurrency];
        issue.account = token[sfTokenIssuer];
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
        return rippleCredit(view, from, to, amount, false, j);

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
    if (auto const stamp = auctionSlot[~sfTimeStamp])
    {
        auto const diff = current - *stamp;
        if (diff < totalSlotTimeSecs)
            return diff / intervalDuration;
    }
    return 0;
}

bool
ammEnabled(Rules const& rules)
{
    return rules.enabled(featureAMM) && rules.enabled(fixUniversalNumber);
}

STAmount
ammAccountHolds(
    ReadView const& view,
    AccountID const& ammAccountID,
    const Issue& issue)
{
    if (isXRP(issue))
    {
        if (auto const sle = view.read(keylet::account(ammAccountID)))
            return (*sle)[sfBalance];
    }
    else if (auto const sle = view.read(
                 keylet::line(ammAccountID, issue.account, issue.currency));
             sle &&
             !isFrozen(view, ammAccountID, issue.currency, issue.account))
    {
        auto amount = (*sle)[sfBalance];
        if (ammAccountID > issue.account)
            amount.negate();
        amount.setIssuer(issue.account);
        return amount;
    }

    return STAmount{issue};
}

}  // namespace ripple
