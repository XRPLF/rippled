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

NotTEC
AMMDeposit::preflight(PreflightContext const& ctx)
{
    if (!ammEnabled(ctx.rules))
        return temDISABLED;

    auto const ret = preflight1(ctx);
    if (!isTesSuccess(ret))
        return ret;

    auto const flags = ctx.tx.getFlags();
    if (flags & tfDepositMask)
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: invalid flags.";
        return temINVALID_FLAG;
    }

    auto const amount = ctx.tx[~sfAmount];
    auto const amount2 = ctx.tx[~sfAmount2];
    auto const ePrice = ctx.tx[~sfEPrice];
    auto const lpTokens = ctx.tx[~sfLPTokenOut];
    // Valid options are:
    //   LPTokens
    //   Amount
    //   Amount and Amount2
    //   AssetLPToken and LPTokens
    //   Amount and EPrice
    if (auto const subTxType = std::bitset<32>(flags & tfDepositSubTx);
        subTxType.none() || subTxType.count() > 1)
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: invalid flags.";
        return temMALFORMED;
    }
    else if (flags & tfLPToken)
    {
        if (!lpTokens || amount || amount2 || ePrice)
            return temMALFORMED;
    }
    else if (flags & tfSingleAsset)
    {
        if (!amount || lpTokens || amount2 || ePrice)
            return temMALFORMED;
    }
    else if (flags & tfTwoAsset)
    {
        if (!amount || !amount2 || lpTokens || ePrice)
            return temMALFORMED;
    }
    else if (flags & tfOneAssetLPToken)
    {
        if (!amount || !lpTokens || amount2 || ePrice)
            return temMALFORMED;
    }
    else if (flags & tfLimitLPToken)
    {
        if (!amount || !ePrice || lpTokens || amount2)
            return temMALFORMED;
    }

    auto const asset = ctx.tx[sfAsset];
    auto const asset2 = ctx.tx[sfAsset2];
    if (auto const res = invalidAMMAssetPair(asset, asset2))
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid asset pair.";
        return res;
    }

    if (amount && amount2 && amount->issue() == amount2->issue())
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: invalid tokens, same issue."
                            << amount->issue() << " " << amount2->issue();
        return temAMM_BAD_TOKENS;
    }

    if (lpTokens && *lpTokens <= beast::zero)
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: invalid LPTokens";
        return temAMM_BAD_TOKENS;
    }

    if (auto const res = invalidAMMAmount(
            amount,
            std::make_optional(std::make_pair(asset, asset2)),
            ePrice.has_value()))
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: invalid Asset1In";
        return res;
    }

    if (auto const res = invalidAMMAmount(
            amount2, std::make_optional(std::make_pair(asset, asset2))))
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: invalid Asset2InAmount";
        return res;
    }

    // must be amount issue
    if (auto const res = invalidAMMAmount(
            ePrice,
            std::make_optional(
                std::make_pair(amount->issue(), amount->issue()))))
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

    auto const ammSle = getAMMSle(ctx.view, ctx.tx[sfAsset], ctx.tx[sfAsset2]);
    if (!ammSle)
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: Invalid asset pair.";
        return terNO_AMM;
    }

    auto const expected = ammHolds(
        ctx.view,
        **ammSle,
        std::nullopt,
        std::nullopt,
        FreezeHandling::fhZERO_IF_FROZEN,
        ctx.j);
    if (!expected)
        return expected.error();
    auto const [amountBalance, amount2Balance, lptAMMBalance] = *expected;
    if (amountBalance <= beast::zero || amount2Balance <= beast::zero ||
        lptAMMBalance <= beast::zero)
    {
        if (isFrozen(ctx.view, amountBalance) ||
            isFrozen(ctx.view, amount2Balance))
        {
            JLOG(ctx.j.debug()) << "AMM Withdraw involves frozen asset.";
            return tecFROZEN;
        }
        JLOG(ctx.j.debug())
            << "AMM Deposit: reserves or tokens balance is zero.";
        return tecAMM_BALANCE;
    }

    // Check account has sufficient funds.
    // Return tesSUCCESS if it does,  error otherwise.
    // Have to check again in deposit() because
    // amounts might be derived based on tokens or
    // limits.
    auto balance = [&](auto const& deposit) -> TER {
        if (isXRP(deposit))
        {
            auto const lpIssue = (**ammSle)[sfLPTokenBalance].issue();
            // Adjust the reserve if LP doesn't have LPToken trustline
            auto const sle = ctx.view.read(
                keylet::line(accountID, lpIssue.account, lpIssue.currency));
            return (xrpLiquid(ctx.view, accountID, !sle, ctx.j) >= deposit)
                ? TER(tesSUCCESS)
                : (!sle ? tecINSUF_RESERVE_LINE : tecAMM_UNFUNDED);
        }
        return (accountHolds(
                    ctx.view,
                    accountID,
                    deposit.issue(),
                    FreezeHandling::fhZERO_IF_FROZEN,
                    ctx.j) >= deposit)
            ? TER(tesSUCCESS)
            : tecAMM_UNFUNDED;
    };

    auto const amount = ctx.tx[~sfAmount];
    auto const amount2 = ctx.tx[~sfAmount2];

    auto checkAmount = [&](auto const& amount) -> TER {
        if (amount)
        {
            // This normally should not happen.
            // Account is not authorized to hold the assets it's depositing,
            // or it doesn't even have a trust line for them
            if (auto const ter =
                    requireAuth(ctx.view, amount->issue(), accountID))
            {
                JLOG(ctx.j.debug())
                    << "AMM Deposit: account is not authorized, "
                    << amount->issue();
                return ter;
            }
            if (auto const ter = balance(*amount))
            {
                JLOG(ctx.j.debug())
                    << "AMM Deposit: account has insufficient funds, "
                    << *amount;
                return ter;
            }
        }
        return tesSUCCESS;
    };

    if (auto const ter = checkAmount(amount))
        return ter;

    if (auto const ter = checkAmount(amount2))
        return ter;

    if (auto const lpTokens = ctx.tx[~sfLPTokenOut];
        lpTokens && lpTokens->issue() != lptAMMBalance.issue())
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: invalid LPTokens.";
        return temAMM_BAD_TOKENS;
    }

    // Check the reserve for LPToken trustline if not LP.
    // We checked above but need to check again if depositing IOU only.
    if (ammLPHolds(ctx.view, **ammSle, accountID, ctx.j) == beast::zero)
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
    auto const amount = ctx_.tx[~sfAmount];
    auto const amount2 = ctx_.tx[~sfAmount2];
    auto const ePrice = ctx_.tx[~sfEPrice];
    auto const lpTokensDeposit = ctx_.tx[~sfLPTokenOut];
    auto ammSle = getAMMSle(sb, ctx_.tx[sfAsset], ctx_.tx[sfAsset2]);
    if (!ammSle)
        return {ammSle.error(), false};
    auto const ammAccountID = (**ammSle)[sfAMMAccount];

    auto const tfee = getTradingFee(ctx_.view(), **ammSle, account_);

    auto const expected = ammHolds(
        sb,
        **ammSle,
        amount ? amount->issue() : std::optional<Issue>{},
        amount2 ? amount2->issue() : std::optional<Issue>{},
        FreezeHandling::fhZERO_IF_FROZEN,
        ctx_.journal);
    if (!expected)
        return {expected.error(), false};
    auto const [amountBalance, amount2Balance, lptAMMBalance] = *expected;

    auto const subTxType = ctx_.tx.getFlags() & tfDepositSubTx;

    auto const [result, newLPTokenBalance] =
        [&,
         &amountBalance = amountBalance,
         &amount2Balance = amount2Balance,
         &lptAMMBalance = lptAMMBalance]() -> std::pair<TER, STAmount> {
        if (subTxType & tfTwoAsset)
            return equalDepositLimit(
                sb,
                ammAccountID,
                amountBalance,
                amount2Balance,
                lptAMMBalance,
                *amount,
                *amount2);
        if (subTxType & tfOneAssetLPToken)
            return singleDepositTokens(
                sb,
                ammAccountID,
                amountBalance,
                *amount,
                lptAMMBalance,
                *lpTokensDeposit,
                tfee);
        if (subTxType & tfLimitLPToken)
            return singleDepositEPrice(
                sb,
                ammAccountID,
                amountBalance,
                *amount,
                lptAMMBalance,
                *ePrice,
                tfee);
        if (subTxType & tfSingleAsset)
            return singleDeposit(
                sb, ammAccountID, amountBalance, lptAMMBalance, *amount, tfee);
        if (subTxType & tfLPToken)
            return equalDepositTokens(
                sb,
                ammAccountID,
                amountBalance,
                amount2Balance,
                lptAMMBalance,
                *lpTokensDeposit);
        // should not happen.
        JLOG(j_.error()) << "AMM Deposit: invalid options.";
        return std::make_pair(tecAMM_FAILED_DEPOSIT, STAmount{});
    }();

    if (result == tesSUCCESS && newLPTokenBalance > beast::zero)
    {
        (*ammSle)->setFieldAmount(sfLPTokenBalance, newLPTokenBalance);
        sb.update(*ammSle);
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
    STAmount const& amountDeposit,
    std::optional<STAmount> const& amount2Deposit,
    STAmount const& lpTokensAMMBalance,
    STAmount const& lpTokensDeposit)
{
    // Check account has sufficient funds.
    // Return true if it does, false otherwise.
    auto balance = [&](auto const& deposit) -> bool {
        if (isXRP(deposit))
        {
            auto const& lpIssue = lpTokensDeposit.issue();
            // Adjust the reserve if LP doesn't have LPToken trustline
            auto const sle = view.read(
                keylet::line(account_, lpIssue.account, lpIssue.currency));
            return xrpLiquid(view, account_, !sle, j_) >= deposit;
        }
        return accountHolds(
                   view,
                   account_,
                   deposit.issue(),
                   FreezeHandling::fhZERO_IF_FROZEN,
                   ctx_.journal) >= deposit;
    };

    // Deposit amountDeposit
    if (!balance(amountDeposit))
    {
        JLOG(ctx_.journal.debug())
            << "AMM Deposit: account has insufficient balance to deposit "
            << amountDeposit;
        return {tecAMM_UNFUNDED, STAmount{}};
    }
    if (amountDeposit <= beast::zero)
    {
        JLOG(ctx_.journal.debug()) << "AMM Deposit: deposit is 0";
        return {tecAMM_FAILED_DEPOSIT, STAmount{}};
    }

    auto res =
        accountSend(view, account_, ammAccount, amountDeposit, ctx_.journal);
    if (res != tesSUCCESS)
    {
        JLOG(ctx_.journal.debug())
            << "AMM Deposit: failed to deposit " << amountDeposit;
        return {res, STAmount{}};
    }

    // Deposit amount2Deposit
    if (amount2Deposit)
    {
        if (!balance(*amount2Deposit))
        {
            JLOG(ctx_.journal.debug())
                << "AMM Deposit: account has insufficient balance to deposit "
                << *amount2Deposit;
            return {tecAMM_UNFUNDED, STAmount{}};
        }
        if (*amount2Deposit <= beast::zero)
        {
            JLOG(ctx_.journal.debug()) << "AMM Deposit: deposit2 is 0";
            return {tecAMM_FAILED_DEPOSIT, STAmount{}};
        }
        res = accountSend(
            view, account_, ammAccount, *amount2Deposit, ctx_.journal);
        if (res != tesSUCCESS)
        {
            JLOG(ctx_.journal.debug())
                << "AMM Deposit: failed to deposit " << *amount2Deposit;
            return {res, STAmount{}};
        }
    }

    // New LPToken balance
    auto const newLPTokenBalance = lpTokensAMMBalance + lpTokensDeposit;
    // Actual tokens to deposit
    // Amount types keep 16 digits. Summing up tokens from LP results in
    // loosing precision in the total balance; i.e. the resulting total balance
    // is less than the actual sum of lp tokens. To adjust for this, subtract
    // old tokens balance from the new one to cancel out the precision loss.
    // A similar adjustment is done in withdraw.
    auto const lpTokensDepositActual = newLPTokenBalance - lpTokensAMMBalance;

    // Deposit LP tokens
    res = accountSend(
        view, ammAccount, account_, lpTokensDepositActual, ctx_.journal);
    if (res != tesSUCCESS)
    {
        JLOG(ctx_.journal.debug()) << "AMM Deposit: failed to deposit LPTokens";
        return {res, STAmount{}};
    }

    return {tesSUCCESS, lpTokensAMMBalance + lpTokensDepositActual};
}

/** Proportional deposit of pools assets in exchange for the specified
 * amount of LPTokens.
 */
std::pair<TER, STAmount>
AMMDeposit::equalDepositTokens(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& amountBalance,
    STAmount const& amount2Balance,
    STAmount const& lptAMMBalance,
    STAmount const& lpTokensDeposit)
{
    try
    {
        auto const frac =
            divide(lpTokensDeposit, lptAMMBalance, lptAMMBalance.issue());
        return deposit(
            view,
            ammAccount,
            multiply(amountBalance, frac, amountBalance.issue()),
            multiply(amount2Balance, frac, amount2Balance.issue()),
            lptAMMBalance,
            lpTokensDeposit);
    }
    catch (std::exception const& e)
    {
        JLOG(j_.error()) << "AMMDeposit::equalDepositTokens exception "
                         << e.what();
    }
    return {tecINTERNAL, STAmount{}};
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
    STAmount const& amountBalance,
    STAmount const& amount2Balance,
    STAmount const& lptAMMBalance,
    STAmount const& amount,
    STAmount const& amount2)
{
    auto frac = Number{amount} / amountBalance;
    auto tokens = toSTAmount(lptAMMBalance.issue(), lptAMMBalance * frac);
    if (tokens == beast::zero)
        return {tecAMM_FAILED_DEPOSIT, STAmount{}};
    auto const amount2Deposit = amount2Balance * frac;
    if (amount2Deposit <= amount2)
        return deposit(
            view,
            ammAccount,
            amount,
            toSTAmount(amount2Balance.issue(), amount2Deposit),
            lptAMMBalance,
            tokens);
    frac = Number{amount2} / amount2Balance;
    tokens = toSTAmount(lptAMMBalance.issue(), lptAMMBalance * frac);
    if (tokens == beast::zero)
        return {tecAMM_FAILED_DEPOSIT, STAmount{}};
    auto const amountDeposit = amountBalance * frac;
    if (amountDeposit <= amount)
        return deposit(
            view,
            ammAccount,
            toSTAmount(amountBalance.issue(), amountDeposit),
            amount2,
            lptAMMBalance,
            tokens);
    return {tecAMM_FAILED_DEPOSIT, STAmount{}};
}

/** Single asset deposit of the amount of asset specified by Asset1In.
 *       t = T * (sqrt(1 + (b - 0.5 * tfee * b) / B) - 1) (3)
 * Use equation 3 @see singleDeposit to compute amount of LPTokens to be issued,
 * given the amount in Asset1In.
 */
std::pair<TER, STAmount>
AMMDeposit::singleDeposit(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& amountBalance,
    STAmount const& lptAMMBalance,
    STAmount const& amount,
    std::uint16_t tfee)
{
    auto const tokens = lpTokensIn(amountBalance, amount, lptAMMBalance, tfee);
    if (tokens == beast::zero)
        return {tecAMM_FAILED_DEPOSIT, STAmount{}};
    return deposit(
        view, ammAccount, amount, std::nullopt, lptAMMBalance, tokens);
}

/** Single asset asset1 is deposited to obtain some share of
 * the AMM instance's pools represented by amount of LPTokens.
 *       b = (((t/T + 1)**2 - 1) / (1 - 0.5 * tfee)) * B (4)
 * Use equation 4 to compute the amount of asset1 to be deposited,
 * given t represented by amount of LPTokens. Fail if b exceeds
 * specified Max amount to deposit.
 */
std::pair<TER, STAmount>
AMMDeposit::singleDepositTokens(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& amountBalance,
    STAmount const& amount,
    STAmount const& lptAMMBalance,
    STAmount const& lpTokensDeposit,
    std::uint16_t tfee)
{
    auto const amountDeposit =
        assetIn(amountBalance, lpTokensDeposit, lptAMMBalance, tfee);
    if (amountDeposit > amount)
        return {tecAMM_FAILED_DEPOSIT, STAmount{}};
    return deposit(
        view,
        ammAccount,
        amountDeposit,
        std::nullopt,
        lptAMMBalance,
        lpTokensDeposit);
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
 * Use equation 3 @see singleDeposit to compute the amount of LPTokens out,
 *   given the amount of Asset1In. Let this be X.
 * Use equation III to compute the effective-price of the trade given
 *   Asset1In amount as the asset in and the LPTokens amount X as asset out.
 *   Let this be Y.
 * If Y <= amount in EPrice:
 *  The amount of asset1 to be deposited is given by amount in Asset1In
 *  The amount of LPTokens to be issued is X
 * If (Y>EPrice) OR (amount in Asset1In does not exist):
 *   Use equations 3 @see singleDeposit & III and the given EPrice to compute
 *     the following two variables:
 *       The amount of asset1 in. Let this be Q
 *       The amount of LPTokens out. Let this be W
 *   The amount of asset1 to be deposited is Q
 *   The amount of LPTokens to be issued is W
 */
std::pair<TER, STAmount>
AMMDeposit::singleDepositEPrice(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& amountBalance,
    STAmount const& amount,
    STAmount const& lptAMMBalance,
    STAmount const& ePrice,
    std::uint16_t tfee)
{
    if (amount != beast::zero)
    {
        auto const tokens =
            lpTokensIn(amountBalance, amount, lptAMMBalance, tfee);
        if (tokens == beast::zero)
            return {tecAMM_FAILED_DEPOSIT, STAmount{}};
        auto const ep = Number{amount} / tokens;
        if (ep <= ePrice)
            return deposit(
                view, ammAccount, amount, std::nullopt, lptAMMBalance, tokens);
    }

    auto const amountDeposit = toSTAmount(
        amountBalance.issue(),
        square(ePrice * lptAMMBalance) * feeMultHalf(tfee) / amountBalance -
            2 * ePrice * lptAMMBalance);
    auto const tokens =
        toSTAmount(lptAMMBalance.issue(), amountDeposit / ePrice);
    return deposit(
        view, ammAccount, amountDeposit, std::nullopt, lptAMMBalance, tokens);
}

}  // namespace ripple
