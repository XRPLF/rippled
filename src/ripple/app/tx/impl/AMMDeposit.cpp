//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2022 Ripple Labs Inc.

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

#include <ripple/app/tx/impl/AMMDeposit.h>

#include <ripple/app/misc/AMM.h>
#include <ripple/app/misc/AMM_formulae.h>
#include <ripple/ledger/Sandbox.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/STAccount.h>

namespace ripple {

TxConsequences
AMMDeposit::makeTxConsequences(PreflightContext const& ctx)
{
    return TxConsequences{ctx.tx};
}

NotTEC
AMMDeposit::preflight(PreflightContext const& ctx)
{
    if (!ammEnabled(ctx.rules))
        return temDISABLED;

    auto const ret = preflight1(ctx);
    if (!isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: invalid flags.";
        return temINVALID_FLAG;
    }

    auto const asset1In = ctx.tx[~sfAsset1In];
    auto const asset2In = ctx.tx[~sfAsset2In];
    auto const ePrice = ctx.tx[~sfEPrice];
    auto const lpTokens = ctx.tx[~sfLPTokenOut];
    // Valid options are:
    //   LPTokens
    //   Asset1In
    //   Asset1In and Asset2In
    //   Asset1In and LPTokens
    //   Asset1In and EPrice
    if ((asset1In && asset2In && (lpTokens || ePrice)) ||
        (asset1In && lpTokens && (asset2In || ePrice)) ||
        (asset1In && ePrice && (asset2In || lpTokens)) ||
        (ePrice && !asset1In) || (asset2In && !asset1In) ||
        (!lpTokens && !asset1In))
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: invalid combination of "
                               "deposit fields.";
        return temBAD_AMM_OPTIONS;
    }

    if (asset1In && asset2In && asset1In->issue() == asset2In->issue())
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: invalid tokens, same issue."
                            << asset1In->issue() << " " << asset2In->issue();
        return temBAD_AMM_TOKENS;
    }

    if (lpTokens && *lpTokens <= beast::zero)
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: invalid LPTokens";
        return temBAD_AMM_TOKENS;
    }

    if (auto const res = invalidAMMAmount(asset1In, (lpTokens || ePrice)))
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: invalid Asset1In";
        return res;
    }

    if (auto const res = invalidAMMAmount(asset2In))
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: invalid Asset2InAmount";
        return res;
    }

    if (auto const res = invalidAMMAmount(ePrice))
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: invalid EPrice";
        return res;
    }

    return preflight2(ctx);
}

TER
AMMDeposit::preclaim(PreclaimContext const& ctx)
{
    auto const accountID = ctx.tx[sfAccount];

    auto const ammSle = ctx.view.read(keylet::amm(ctx.tx[sfAMMID]));
    if (!ammSle)
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: Invalid AMMID.";
        return terNO_ACCOUNT;
    }

    auto const asset1In = ctx.tx[~sfAsset1In];
    auto const asset2In = ctx.tx[~sfAsset2In];

    auto const [issue1, issue2] = getTokensIssue(*ammSle);

    if (asset1In)
    {
        if (asset1In->issue() != issue1 && asset1In->issue() != issue2)
        {
            JLOG(ctx.j.debug())
                << "AMM Deposit: token mismatch, " << asset1In->issue() << " "
                << issue1 << " " << issue2;
            return temBAD_AMM_TOKENS;
        }

        if (auto const ter =
                requireAuth(ctx.view, asset1In->issue(), accountID);
            ter != tesSUCCESS)
        {
            JLOG(ctx.j.debug()) << "AMM Deposit: account is not authorized, "
                                << asset1In->issue();
            return ter;
        }
    }

    if (asset2In)
    {
        if (asset2In->issue() != issue1 && asset2In->issue() != issue2)
        {
            JLOG(ctx.j.debug())
                << "AMM Deposit: token mismatch, " << asset2In->issue() << " "
                << issue1 << " " << issue2;
            return temBAD_AMM_TOKENS;
        }

        if (auto const ter =
                requireAuth(ctx.view, asset2In->issue(), accountID);
            ter != tesSUCCESS)
        {
            JLOG(ctx.j.debug()) << "AMM Deposit: account is not authorized, "
                                << asset2In->issue();
            return ter;
        }
    }

    if (isFrozen(ctx.view, asset1In) || isFrozen(ctx.view, asset2In))
    {
        JLOG(ctx.j.debug()) << "AMM Deposit involves frozen asset.";
        return tecFROZEN;
    }

    auto const expected =
        ammHolds(ctx.view, *ammSle, std::nullopt, std::nullopt, ctx.j);
    if (!expected)
        return expected.error();
    auto const [asset1, asset2, lptAMMBalance] = *expected;
    if (asset1 <= beast::zero || asset2 <= beast::zero ||
        lptAMMBalance <= beast::zero)
    {
        JLOG(ctx.j.debug())
            << "AMM Deposit: reserves or tokens balance is zero.";
        return tecAMM_BALANCE;
    }

    if (auto const lpTokens = ctx.tx[~sfLPTokenOut];
        lpTokens && lpTokens->issue() != lptAMMBalance.issue())
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: invalid LPTokens.";
        return temBAD_AMM_TOKENS;
    }

    // Check the reserve for LPToken trustline if not LP
    if (lpHolds(ctx.view, (*ammSle)[sfAMMAccount], accountID, ctx.j) ==
        beast::zero)
    {
        STAmount const xrpBalance = xrpLiquid(ctx.view, accountID, 1, ctx.j);
        // Insufficient reserve
        if (xrpBalance <= beast::zero)
        {
            JLOG(ctx.j.debug()) << "AMM Instance: insufficient reserves";
            return tecINSUF_RESERVE_LINE;
        }
    }

    return tesSUCCESS;
}

std::pair<TER, bool>
AMMDeposit::applyGuts(Sandbox& sb)
{
    auto const asset1In = ctx_.tx[~sfAsset1In];
    auto const asset2In = ctx_.tx[~sfAsset2In];
    auto const ePrice = ctx_.tx[~sfEPrice];
    auto const lpTokensDeposit = ctx_.tx[~sfLPTokenOut];
    auto ammSle = sb.peek(keylet::amm(ctx_.tx[sfAMMID]));
    if (!ammSle)
        return {tecINTERNAL, false};
    auto const ammAccountID = (*ammSle)[sfAMMAccount];

    auto const tfee = getTradingFee(ctx_.view(), *ammSle, account_);

    auto const expected = ammHolds(
        sb,
        *ammSle,
        asset1In ? asset1In->issue() : std::optional<Issue>{},
        asset2In ? asset2In->issue() : std::optional<Issue>{},
        ctx_.journal);
    if (!expected)
        return {expected.error(), false};
    auto const [asset1, asset2, lptAMMBalance] = *expected;

    auto const [result, depositedTokens] =
        [&,
         &asset1 = asset1,
         &asset2 = asset2,
         &lptAMMBalance = lptAMMBalance]() -> std::pair<TER, STAmount> {
        if (asset1In)
        {
            if (asset2In)
                return equalDepositLimit(
                    sb,
                    ammAccountID,
                    asset1,
                    asset2,
                    lptAMMBalance,
                    *asset1In,
                    *asset2In);
            else if (lpTokensDeposit)
                return singleDepositTokens(
                    sb,
                    ammAccountID,
                    asset1,
                    lptAMMBalance,
                    *lpTokensDeposit,
                    tfee);
            else if (ePrice)
                return singleDepositEPrice(
                    sb,
                    ammAccountID,
                    asset1,
                    *asset1In,
                    lptAMMBalance,
                    *ePrice,
                    tfee);
            else
                return singleDeposit(
                    sb, ammAccountID, asset1, lptAMMBalance, *asset1In, tfee);
        }
        else if (lpTokensDeposit)
            return equalDepositTokens(
                sb,
                ammAccountID,
                asset1,
                asset2,
                lptAMMBalance,
                *lpTokensDeposit);
        // should not happen.
        JLOG(j_.error()) << "AMM Deposit: invalid options.";
        return std::make_pair(tecAMM_FAILED_DEPOSIT, STAmount{});
    }();

    if (result == tesSUCCESS && depositedTokens != beast::zero)
    {
        ammSle->setFieldAmount(
            sfLPTokenBalance, lptAMMBalance + depositedTokens);
        sb.update(ammSle);
    }

    return {result, result == tesSUCCESS};
}

TER
AMMDeposit::doApply()
{
    // This is the ledger view that we work against. Transactions are applied
    // as we go on processing transactions.
    Sandbox sb(&ctx_.view());

    // This is a ledger with just the fees paid and any unfunded or expired
    // offers we encounter removed. It's used when handling Fill-or-Kill offers,
    // if the order isn't going to be placed, to avoid wasting the work we did.
    Sandbox sbCancel(&ctx_.view());

    auto const result = applyGuts(sb);
    if (result.second)
        sb.apply(ctx_.rawView());
    else
        sbCancel.apply(ctx_.rawView());

    return result.first;
}

std::pair<TER, STAmount>
AMMDeposit::deposit(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Deposit,
    std::optional<STAmount> const& asset2Deposit,
    STAmount const& lpTokensDeposit)
{
    // Check account has sufficient funds.
    // Return true if it does, false otherwise.
    auto balance = [&](auto const& assetDeposit) -> bool {
        if (isXRP(assetDeposit))
        {
            auto const& lpIssue = lpTokensDeposit.issue();
            // Adjust the reserve if LP doesn't have LPToken trustline
            auto const sle = view.read(
                keylet::line(account_, lpIssue.account, lpIssue.currency));
            return xrpLiquid(view, account_, !sle, j_) >= assetDeposit;
        }
        return accountHolds(
                   view,
                   account_,
                   assetDeposit.issue().currency,
                   assetDeposit.issue().account,
                   FreezeHandling::fhZERO_IF_FROZEN,
                   ctx_.journal) >= assetDeposit;
    };

    // Deposit asset1Deposit
    if (!balance(asset1Deposit))
    {
        JLOG(ctx_.journal.debug())
            << "AMM Deposit: account has insufficient balance to deposit "
            << asset1Deposit;
        return {tecUNFUNDED_AMM, STAmount{}};
    }
    auto res = ammSend(view, account_, ammAccount, asset1Deposit, ctx_.journal);
    if (res != tesSUCCESS)
    {
        JLOG(ctx_.journal.debug())
            << "AMM Deposit: failed to deposit " << asset1Deposit;
        return {res, STAmount{}};
    }

    // Deposit asset2Deposit
    if (asset2Deposit)
    {
        if (!balance(*asset2Deposit))
        {
            JLOG(ctx_.journal.debug())
                << "AMM Deposit: account has insufficient balance to deposit "
                << *asset2Deposit;
            return {tecUNFUNDED_AMM, STAmount{}};
        }
        res = ammSend(view, account_, ammAccount, *asset2Deposit, ctx_.journal);
        if (res != tesSUCCESS)
        {
            JLOG(ctx_.journal.debug())
                << "AMM Deposit: failed to deposit " << *asset2Deposit;
            return {res, STAmount{}};
        }
    }

    // Deposit LP tokens
    res =
        accountSend(view, ammAccount, account_, lpTokensDeposit, ctx_.journal);
    if (res != tesSUCCESS)
    {
        JLOG(ctx_.journal.debug()) << "AMM Deposit: failed to deposit LPTokens";
        return {res, STAmount{}};
    }

    return {tesSUCCESS, lpTokensDeposit};
}

/** Proportional deposit of pools assets in exchange for the specified
 * amount of LPTokens.
 */
std::pair<TER, STAmount>
AMMDeposit::equalDepositTokens(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& lptAMMBalance,
    STAmount const& lpTokensDeposit)
{
    auto const frac =
        divide(lpTokensDeposit, lptAMMBalance, lptAMMBalance.issue());
    return deposit(
        view,
        ammAccount,
        multiply(asset1Balance, frac, asset1Balance.issue()),
        multiply(asset2Balance, frac, asset2Balance.issue()),
        lpTokensDeposit);
}

/** Proportional deposit of pool assets with the constraints on the maximum
 * amount of each asset that the trader is willing to deposit.
 *      a = (t/T) * A (1)
 *      b = (t/T) * B (2)
 *     where
 *      A,B: current pool composition
 *      T: current balance of outstanding LPTokens
 *      a: balance of asset A being added
 *      b: balance of asset B being added
 *      t: balance of LPTokens issued to LP after a successful transaction
 * Use equation 1 to compute the amount of , given the amount in Asset1In.
 *     Let this be Z
 * Use equation 2 to compute the amount of asset2, given  t~Z. Let
 *     the computed amount of asset2 be X.
 * If X <= amount in Asset2In:
 *   The amount of asset1 to be deposited is the one specified in Asset1In
 *   The amount of asset2 to be deposited is X
 *   The amount of LPTokens to be issued is Z
 * If X > amount in Asset2In:
 *   Use equation 2 to compute , given the amount in Asset2In. Let this be W
 *   Use equation 1 to compute the amount of asset1, given t~W from above.
 *     Let the computed amount of asset1 be Y
 *   If Y <= amount in Asset1In:
 *     The amount of asset1 to be deposited is Y
 *     The amount of asset2 to be deposited is the one specified in Asset2In
 *     The amount of LPTokens to be issued is W
 * else, failed transaction
 */
std::pair<TER, STAmount>
AMMDeposit::equalDepositLimit(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& lptAMMBalance,
    STAmount const& asset1In,
    STAmount const& asset2In)
{
    auto frac = Number{asset1In} / asset1Balance;
    auto tokens = toSTAmount(lptAMMBalance.issue(), lptAMMBalance * frac);
    if (tokens == beast::zero)
        return {tecAMM_INVALID_TOKENS, STAmount{}};
    auto const asset2Deposit = asset2Balance * frac;
    if (asset2Deposit <= asset2In)
        return deposit(
            view,
            ammAccount,
            asset1In,
            toSTAmount(asset2Balance.issue(), asset2Deposit),
            tokens);
    frac = Number{asset2In} / asset2Balance;
    tokens = toSTAmount(lptAMMBalance.issue(), lptAMMBalance * frac);
    if (tokens == beast::zero)
        return {tecAMM_INVALID_TOKENS, STAmount{}};
    auto const asset1Deposit = asset1Balance * frac;
    if (asset1Deposit <= asset1In)
        return deposit(
            view,
            ammAccount,
            toSTAmount(asset1Balance.issue(), asset1Deposit),
            asset2In,
            tokens);
    return {tecAMM_FAILED_DEPOSIT, STAmount{}};
}

/** Single asset deposit of the amount of asset specified by Asset1In.
 *       t = T * (sqrt(1 + (b - 0.5 * tfee * b) / B) - 1) (3)
 * Use equation 3 to compute amount of LPTokens to be issued, given
 * the amount in Asset1In.
 */
std::pair<TER, STAmount>
AMMDeposit::singleDeposit(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Balance,
    STAmount const& lptAMMBalance,
    STAmount const& asset1In,
    std::uint16_t tfee)
{
    auto const tokens =
        lpTokensIn(asset1Balance, asset1In, lptAMMBalance, tfee);
    if (tokens == beast::zero)
        return {tecAMM_INVALID_TOKENS, STAmount{}};
    return deposit(view, ammAccount, asset1In, std::nullopt, tokens);
}

/** Single asset asset1 is deposited to obtain some share of
 * the AMM instance's pools represented by amount of LPTokens.
 *       b = (((t/T + 1)**2 - 1) / (1 - 0.5 * tfee)) * B (4)
 * Use equation 4 to compute the amount of asset1 to be deposited,
 * given t represented by amount of LPTokens.
 */
std::pair<TER, STAmount>
AMMDeposit::singleDepositTokens(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Balance,
    STAmount const& lptAMMBalance,
    STAmount const& lpTokensDeposit,
    std::uint16_t tfee)
{
    auto const asset1Deposit =
        assetIn(asset1Balance, lpTokensDeposit, lptAMMBalance, tfee);
    return deposit(
        view, ammAccount, asset1Deposit, std::nullopt, lpTokensDeposit);
}

/** Single asset deposit with two constraints.
 * a. Amount of asset1 if specified in Asset1In specifies the maximum
 *     amount of asset1 that the trader is willing to deposit.
 * b. The effective-price of the LPToken traded out does not exceed
 *     the specified EPrice.
 *       The effective price (EP) of a trade is defined as the ratio
 *       of the tokens the trader sold or swapped in (Token B) and
 *       the token they got in return or swapped out (Token A).
 *       EP(B/A) = b/a (III)
 * Use equation 3 to compute the amount of LPTokens out, given the amount
 *   of Asset1In. Let this be X.
 * Use equation III to compute the effective-price of the trade given
 *   Asset1In amount as the asset in and the LPTokens amount X as asset out.
 *   Let this be Y.
 * If Y <= amount in EPrice:
 *  The amount of asset1 to be deposited is given by amount in Asset1In
 *  The amount of LPTokens to be issued is X
 * If (Y>EPrice) OR (amount in Asset1In does not exist):
 *   Use equations 3 & III and the given EPrice to compute the following
 *     two variables:
 *       The amount of asset1 in. Let this be Q
 *       The amount of LPTokens out. Let this be W
 *   The amount of asset1 to be deposited is Q
 *   The amount of LPTokens to be issued is W
 */
std::pair<TER, STAmount>
AMMDeposit::singleDepositEPrice(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Balance,
    STAmount const& asset1In,
    STAmount const& lptAMMBalance,
    STAmount const& ePrice,
    std::uint16_t tfee)
{
    if (asset1In != beast::zero)
    {
        auto const tokens =
            lpTokensIn(asset1Balance, asset1In, lptAMMBalance, tfee);
        if (tokens == beast::zero)
            return {tecAMM_FAILED_DEPOSIT, STAmount{}};
        auto const ep = Number{asset1In} / tokens;
        if (ep <= ePrice)
            return deposit(view, ammAccount, asset1In, std::nullopt, tokens);
    }

    auto const asset1In_ = toSTAmount(
        asset1Balance.issue(),
        square(ePrice * lptAMMBalance) * feeMultHalf(tfee) / asset1Balance -
            2 * ePrice * lptAMMBalance);
    auto const tokens = toSTAmount(lptAMMBalance.issue(), asset1In_ / ePrice);
    return deposit(view, ammAccount, asset1In_, std::nullopt, tokens);
}

}  // namespace ripple