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
#include <ripple/protocol/STArray.h>

namespace ripple {

uint256
calcAMMHash(std::uint8_t weight1, Issue const& issue1, Issue const& issue2)
{
    if (issue1 < issue2)
        return sha512Half(
            weight1,
            issue1.account,
            issue1.currency,
            issue2.account,
            issue2.currency);
    return sha512Half(
        static_cast<std::uint8_t>(100 - weight1),
        issue2.account,
        issue2.currency,
        issue1.account,
        issue1.currency);
}

std::pair<uint256, std::uint8_t>
calcAMMHashAndWeight(int weight1, Issue const& issue1, Issue const& issue2)
{
    auto const weight2 = 100 - weight1;
    auto const weight = issue1 < issue2 ? weight1 : weight2;
    return std::make_pair(calcAMMHash(weight1, issue1, issue2), weight);
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
getAMMPoolBalances(
    ReadView const& view,
    AccountID const& ammAccountID,
    Issue const& in,
    Issue const& out,
    beast::Journal const j)
{
    auto const assetInBalance = accountHolds(
        view,
        ammAccountID,
        in.currency,
        in.account,
        FreezeHandling::fhZERO_IF_FROZEN,
        j);
    auto const assetOutBalance = accountHolds(
        view,
        ammAccountID,
        out.currency,
        out.account,
        FreezeHandling::fhZERO_IF_FROZEN,
        j);
    return std::make_pair(assetInBalance, assetOutBalance);
}

template <typename F>
void
forEeachAMMTrustLine(ReadView const& view, AccountID const& ammAccountID, F&& f)
{
    forEachItem(view, ammAccountID, [&](std::shared_ptr<SLE const> const& sle) {
        if (sle && sle->getType() == ltRIPPLE_STATE)
        {
            auto const line =
                ripple::PathFindTrustLine::makeItem(ammAccountID, sle);
            f(*line);
        }
    });
}

std::tuple<STAmount, STAmount, STAmount>
getAMMBalances(
    ReadView const& view,
    AccountID const& ammAccountID,
    std::optional<AccountID> const& lpAccountID,
    std::optional<Issue> const& issue1,
    std::optional<Issue> const& issue2,
    beast::Journal const j)
{
    std::optional<Issue> issue1Opt{};
    std::optional<Issue> issue2Opt = xrpIssue();
    auto const lptIssue = calcLPTIssue(ammAccountID);
    // tokens of all LP's
    STAmount ammLPTokens{lptIssue, 0};
    forEeachAMMTrustLine(
        view, ammAccountID, [&](ripple::PathFindTrustLine const& line) {
            auto balance = line.getBalance();
            if (balance.getCurrency() == lptIssue.currency)
            {
                balance.setIssuer(ammAccountID);
                if (balance.negative())
                    balance.negate();
                ammLPTokens += balance;
                return true;
            }
            if (!issue1Opt)
            {
                issue1Opt = balance.issue();
                issue1Opt->account = line.getAccountIDPeer();
            }
            else
            {
                issue2Opt = balance.issue();
                issue2Opt->account = line.getAccountIDPeer();
            }
            return true;
        });
    auto issue = [](auto const& issue) { return issue ? *issue : noIssue(); };
    if (!issue1Opt)
        return std::make_tuple(
            STAmount{issue(issue1), 0},
            STAmount{issue(issue2), 0},
            STAmount{lptIssue, 0});
    // re-order if needed
    if ((issue1 && *issue1Opt != *issue1) ||
        (!issue1 && issue2 && *issue2Opt != *issue2))
        issue1Opt.swap(issue2Opt);
    // validate issues
    if ((issue1 && *issue1Opt != *issue1) || (issue2 && *issue2Opt != *issue2))
        return std::make_tuple(
            STAmount{issue(issue1), 0},
            STAmount{issue(issue2), 0},
            STAmount{lptIssue, 0});
    auto const [balance1, balance2] =
        getAMMPoolBalances(view, ammAccountID, *issue1Opt, *issue2Opt, j);

    if (!lpAccountID)
        return {balance1, balance2, ammLPTokens};

    auto const lpTokens = getLPTokens(view, ammAccountID, *lpAccountID, j);
    if (lpTokens <= beast::zero)
        return std::make_tuple(
            STAmount{*issue1Opt, 0},
            STAmount{*issue2Opt, 0},
            STAmount{lptIssue, 0});
    auto const frac = divide(lpTokens, ammLPTokens, noIssue());
    auto const lpBalance1 = multiply(balance1, frac, balance1.issue());
    auto const lpBalance2 = multiply(balance2, frac, balance2.issue());
    return {lpBalance1, lpBalance2, lpTokens};
}

STAmount
getLPTokens(
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
validAmount(std::optional<STAmount> const& a, bool zero)
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
getAMMSle(ReadView const& view, uint256 ammHash)
{
    auto const sle = view.read(keylet::amm(ammHash));
    if (!sle || !view.read(keylet::account(sle->getAccountID(sfAMMAccount))))
        return nullptr;
    return sle;
}

std::uint8_t
orderWeight(std::uint8_t weight, Issue const& issue1, Issue const& issue2)
{
    if (issue1 < issue2)
        return weight;
    else
        return 100 - weight;
}

bool
requireAuth(ReadView const& view, Issue const& issue, AccountID const& account)
{
    if (isXRP(issue) || issue.account == account)
        return false;

    if (auto const issuerAccount = view.read(keylet::account(issue.account));
        issuerAccount && (*issuerAccount)[sfFlags] & lsfRequireAuth)
    {
        if (auto const trustLine =
                view.read(keylet::line(account, issue.account, issue.currency));
            trustLine)
            return !(
                (*trustLine)[sfFlags] &
                ((account > issue.account) ? lsfLowAuth : lsfHighAuth));
    }

    return false;
}

}  // namespace ripple
