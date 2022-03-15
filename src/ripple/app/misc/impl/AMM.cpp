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

namespace ripple {

AccountID
calcAMMGroupHash(Issue const& issue1, Issue const& issue2)
{
    if (isXRP(issue1.currency))
        return calcAccountID(issue2);
    else if (isXRP(issue2.currency))
        return calcAccountID(issue1);
    else if (issue1 > issue2)
        return calcAccountID(
            issue1.account, issue1.currency, issue2.account, issue2.currency);
    return calcAccountID(
        issue2.account, issue2.currency, issue1.account, issue1.currency);
}

std::pair<AccountID, std::uint8_t>
calcAMMAccountIDAndWeight(int weight1, Issue const& issue1, Issue const& issue2)
{
    auto const weight2 = 100 - weight1;
    if (isXRP(issue1.currency))
        return std::make_pair(calcAccountID(weight2, issue2), weight2);
    else if (isXRP(issue2.currency))
        return std::make_pair(calcAccountID(weight1, issue1), weight1);
    else if (issue1 > issue2)
        return std::make_pair(
            calcAccountID(
                weight1,
                issue1.account,
                issue1.currency,
                issue2.account,
                issue2.currency),
            weight1);
    return std::make_pair(
        calcAccountID(
            weight2,
            issue2.account,
            issue2.currency,
            issue1.account,
            issue1.currency),
        weight2);
}

std::pair<std::uint8_t, std::uint8_t>
canonicalWeights(int weight, Issue const& in, Issue const& out)
{
    if (isXRP(in.currency))
        return std::make_pair(100 - weight, weight);
    else if (isXRP(out.currency))
        return std::make_pair(weight, 100 - weight);
    else if (in > out)
        return std::make_pair(weight, 100 - weight);
    return std::make_pair(100 - weight, weight);
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
    if (!issue1Opt)
        return std::make_tuple(
            STAmount{*issue1Opt, 0},
            STAmount{*issue2Opt, 0},
            STAmount{lptIssue, 0});
    // re-order if needed
    if (issue1 && *issue1Opt != *issue1)
        issue1Opt.swap(issue2Opt);
    // validate issues
    if ((issue1 && *issue1Opt != *issue1) || (issue2 && *issue2Opt != *issue2))
        return std::make_tuple(
            STAmount{*issue1Opt, 0},
            STAmount{*issue2Opt, 0},
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

bool
validLPTokens(STAmount const& lptAMMBalance, STAmount const& tokens)
{
    auto const pct = multiply(
        divide(tokens, lptAMMBalance, tokens.issue()),
        STAmount{tokens.issue(), 100},
        tokens.issue());
    return pct != beast::zero && pct <= STAmount{tokens.issue(), 30};
}

}  // namespace ripple
