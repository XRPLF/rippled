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

#include <xrpld/app/tx/detail/AMMDeposit.h>

#include <xrpld/app/misc/AMMHelpers.h>
#include <xrpld/app/misc/AMMUtils.h>
#include <xrpld/ledger/Sandbox.h>
#include <xrpld/ledger/View.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/TxFlags.h>

#include <bit>

namespace ripple {

NotTEC
AMMDeposit::preflight(PreflightContext const& ctx)
{
    if (!ammEnabled(ctx.rules))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
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
    auto const tradingFee = ctx.tx[~sfTradingFee];
    // Valid options for the flags are:
    //   tfLPTokens: LPTokenOut, [Amount, Amount2]
    //   tfSingleAsset: Amount, [LPTokenOut]
    //   tfTwoAsset: Amount, Amount2, [LPTokenOut]
    //   tfTwoAssetIfEmpty: Amount, Amount2, [sfTradingFee]
    //   tfOnAssetLPToken: Amount and LPTokenOut
    //   tfLimitLPToken: Amount and EPrice
    if (std::popcount(flags & tfDepositSubTx) != 1)
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: invalid flags.";
        return temMALFORMED;
    }
    if (flags & tfLPToken)
    {
        // if included then both amount and amount2 are deposit min
        if (!lpTokens || ePrice || (amount && !amount2) ||
            (!amount && amount2) || tradingFee)
            return temMALFORMED;
    }
    else if (flags & tfSingleAsset)
    {
        // if included then lpTokens is deposit min
        if (!amount || amount2 || ePrice || tradingFee)
            return temMALFORMED;
    }
    else if (flags & tfTwoAsset)
    {
        // if included then lpTokens is deposit min
        if (!amount || !amount2 || ePrice || tradingFee)
            return temMALFORMED;
    }
    else if (flags & tfOneAssetLPToken)
    {
        if (!amount || !lpTokens || amount2 || ePrice || tradingFee)
            return temMALFORMED;
    }
    else if (flags & tfLimitLPToken)
    {
        if (!amount || !ePrice || lpTokens || amount2 || tradingFee)
            return temMALFORMED;
    }
    else if (flags & tfTwoAssetIfEmpty)
    {
        if (!amount || !amount2 || ePrice || lpTokens)
            return temMALFORMED;
    }

    auto const asset = ctx.tx[sfAsset];
    auto const asset2 = ctx.tx[sfAsset2];
    if (auto const res = invalidAMMAssetPair(asset, asset2))
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: invalid asset pair.";
        return res;
    }

    if (amount && amount2 && amount->issue() == amount2->issue())
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: invalid tokens, same issue."
                            << amount->issue() << " " << amount2->issue();
        return temBAD_AMM_TOKENS;
    }

    if (lpTokens && *lpTokens <= beast::zero)
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: invalid LPTokens";
        return temBAD_AMM_TOKENS;
    }

    if (amount)
    {
        if (auto const res = invalidAMMAmount(
                *amount,
                std::make_optional(std::make_pair(asset, asset2)),
                ePrice.has_value()))
        {
            JLOG(ctx.j.debug()) << "AMM Deposit: invalid amount";
            return res;
        }
    }

    if (amount2)
    {
        if (auto const res = invalidAMMAmount(
                *amount2, std::make_optional(std::make_pair(asset, asset2))))
        {
            JLOG(ctx.j.debug()) << "AMM Deposit: invalid amount2";
            return res;
        }
    }

    // must be amount issue
    if (amount && ePrice)
    {
        if (auto const res = invalidAMMAmount(
                *ePrice,
                std::make_optional(
                    std::make_pair(amount->issue(), amount->issue()))))
        {
            JLOG(ctx.j.debug()) << "AMM Deposit: invalid EPrice";
            return res;
        }
    }

    if (tradingFee > TRADING_FEE_THRESHOLD)
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: invalid trading fee.";
        return temBAD_FEE;
    }

    return preflight2(ctx);
}

TER
AMMDeposit::preclaim(PreclaimContext const& ctx)
{
    auto const accountID = ctx.tx[sfAccount];

    auto const ammSle =
        ctx.view.read(keylet::amm(ctx.tx[sfAsset], ctx.tx[sfAsset2]));
    if (!ammSle)
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: Invalid asset pair.";
        return terNO_AMM;
    }

    auto const expected = ammHolds(
        ctx.view,
        *ammSle,
        std::nullopt,
        std::nullopt,
        FreezeHandling::fhIGNORE_FREEZE,
        ctx.j);
    if (!expected)
        return expected.error();  // LCOV_EXCL_LINE
    auto const [amountBalance, amount2Balance, lptAMMBalance] = *expected;
    if (ctx.tx.getFlags() & tfTwoAssetIfEmpty)
    {
        if (lptAMMBalance != beast::zero)
            return tecAMM_NOT_EMPTY;
        if (amountBalance != beast::zero || amount2Balance != beast::zero)
        {
            // LCOV_EXCL_START
            JLOG(ctx.j.debug()) << "AMM Deposit: tokens balance is not zero.";
            return tecINTERNAL;
            // LCOV_EXCL_STOP
        }
    }
    else
    {
        if (lptAMMBalance == beast::zero)
            return tecAMM_EMPTY;
        if (amountBalance <= beast::zero || amount2Balance <= beast::zero ||
            lptAMMBalance < beast::zero)
        {
            // LCOV_EXCL_START
            JLOG(ctx.j.debug())
                << "AMM Deposit: reserves or tokens balance is zero.";
            return tecINTERNAL;
            // LCOV_EXCL_STOP
        }
    }

    // Check account has sufficient funds.
    // Return tesSUCCESS if it does,  error otherwise.
    // Have to check again in deposit() because
    // amounts might be derived based on tokens or
    // limits.
    auto balance = [&](auto const& deposit) -> TER {
        if (isXRP(deposit))
        {
            auto const lpIssue = (*ammSle)[sfLPTokenBalance].issue();
            // Adjust the reserve if LP doesn't have LPToken trustline
            auto const sle = ctx.view.read(
                keylet::line(accountID, lpIssue.account, lpIssue.currency));
            if (xrpLiquid(ctx.view, accountID, !sle, ctx.j) >= deposit)
                return TER(tesSUCCESS);
            if (sle)
                return tecUNFUNDED_AMM;
            return tecINSUF_RESERVE_LINE;
        }
        return (accountID == deposit.issue().account ||
                accountHolds(
                    ctx.view,
                    accountID,
                    deposit.issue(),
                    FreezeHandling::fhIGNORE_FREEZE,
                    ctx.j) >= deposit)
            ? TER(tesSUCCESS)
            : tecUNFUNDED_AMM;
    };

    if (ctx.view.rules().enabled(featureAMMClawback))
    {
        // Check if either of the assets is frozen, AMMDeposit is not allowed
        // if either asset is frozen
        auto checkAsset = [&](Issue const& asset) -> TER {
            if (auto const ter = requireAuth(ctx.view, asset, accountID))
            {
                JLOG(ctx.j.debug())
                    << "AMM Deposit: account is not authorized, " << asset;
                return ter;
            }

            if (isFrozen(ctx.view, accountID, asset))
            {
                JLOG(ctx.j.debug())
                    << "AMM Deposit: account or currency is frozen, "
                    << to_string(accountID) << " " << to_string(asset.currency);

                return tecFROZEN;
            }

            return tesSUCCESS;
        };

        if (auto const ter = checkAsset(ctx.tx[sfAsset]))
            return ter;

        if (auto const ter = checkAsset(ctx.tx[sfAsset2]))
            return ter;
    }

    auto const amount = ctx.tx[~sfAmount];
    auto const amount2 = ctx.tx[~sfAmount2];
    auto const ammAccountID = ammSle->getAccountID(sfAccount);

    auto checkAmount = [&](std::optional<STAmount> const& amount,
                           bool checkBalance) -> TER {
        if (amount)
        {
            // This normally should not happen.
            // Account is not authorized to hold the assets it's depositing,
            // or it doesn't even have a trust line for them
            if (auto const ter =
                    requireAuth(ctx.view, amount->issue(), accountID))
            {
                // LCOV_EXCL_START
                JLOG(ctx.j.debug())
                    << "AMM Deposit: account is not authorized, "
                    << amount->issue();
                return ter;
                // LCOV_EXCL_STOP
            }
            // AMM account or currency frozen
            if (isFrozen(ctx.view, ammAccountID, amount->issue()))
            {
                JLOG(ctx.j.debug())
                    << "AMM Deposit: AMM account or currency is frozen, "
                    << to_string(accountID);
                return tecFROZEN;
            }
            // Account frozen
            if (isIndividualFrozen(ctx.view, accountID, amount->issue()))
            {
                JLOG(ctx.j.debug()) << "AMM Deposit: account is frozen, "
                                    << to_string(accountID) << " "
                                    << to_string(amount->issue().currency);
                return tecFROZEN;
            }
            if (checkBalance)
            {
                if (auto const ter = balance(*amount))
                {
                    JLOG(ctx.j.debug())
                        << "AMM Deposit: account has insufficient funds, "
                        << *amount;
                    return ter;
                }
            }
        }
        return tesSUCCESS;
    };

    // amount and amount2 are deposit min in case of tfLPToken
    if (!(ctx.tx.getFlags() & tfLPToken))
    {
        if (auto const ter = checkAmount(amount, true))
            return ter;

        if (auto const ter = checkAmount(amount2, true))
            return ter;
    }
    else
    {
        if (auto const ter = checkAmount(amountBalance, false))
            return ter;
        if (auto const ter = checkAmount(amount2Balance, false))
            return ter;
    }

    // Equal deposit lp tokens
    if (auto const lpTokens = ctx.tx[~sfLPTokenOut];
        lpTokens && lpTokens->issue() != lptAMMBalance.issue())
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: invalid LPTokens.";
        return temBAD_AMM_TOKENS;
    }

    // Check the reserve for LPToken trustline if not LP.
    // We checked above but need to check again if depositing IOU only.
    if (ammLPHolds(ctx.view, *ammSle, accountID, ctx.j) == beast::zero)
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
    auto ammSle = sb.peek(keylet::amm(ctx_.tx[sfAsset], ctx_.tx[sfAsset2]));
    if (!ammSle)
        return {tecINTERNAL, false};  // LCOV_EXCL_LINE
    auto const ammAccountID = (*ammSle)[sfAccount];

    auto const expected = ammHolds(
        sb,
        *ammSle,
        amount ? amount->issue() : std::optional<Issue>{},
        amount2 ? amount2->issue() : std::optional<Issue>{},
        FreezeHandling::fhZERO_IF_FROZEN,
        ctx_.journal);
    if (!expected)
        return {expected.error(), false};  // LCOV_EXCL_LINE
    auto const [amountBalance, amount2Balance, lptAMMBalance] = *expected;
    auto const tfee = (lptAMMBalance == beast::zero)
        ? ctx_.tx[~sfTradingFee].value_or(0)
        : getTradingFee(ctx_.view(), *ammSle, account_);

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
                *amount2,
                lpTokensDeposit,
                tfee);
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
                sb,
                ammAccountID,
                amountBalance,
                lptAMMBalance,
                *amount,
                lpTokensDeposit,
                tfee);
        if (subTxType & tfLPToken)
            return equalDepositTokens(
                sb,
                ammAccountID,
                amountBalance,
                amount2Balance,
                lptAMMBalance,
                *lpTokensDeposit,
                amount,
                amount2,
                tfee);
        if (subTxType & tfTwoAssetIfEmpty)
            return equalDepositInEmptyState(
                sb,
                ammAccountID,
                *amount,
                *amount2,
                lptAMMBalance.issue(),
                tfee);
        // should not happen.
        // LCOV_EXCL_START
        JLOG(j_.error()) << "AMM Deposit: invalid options.";
        return std::make_pair(tecINTERNAL, STAmount{});
        // LCOV_EXCL_STOP
    }();

    if (result == tesSUCCESS)
    {
        ASSERT(
            newLPTokenBalance > beast::zero,
            "ripple::AMMDeposit::applyGuts : valid new LP token balance");
        ammSle->setFieldAmount(sfLPTokenBalance, newLPTokenBalance);
        // LP depositing into AMM empty state gets the auction slot
        // and the voting
        if (lptAMMBalance == beast::zero)
            initializeFeeAuctionVote(
                sb, ammSle, account_, lptAMMBalance.issue(), tfee);

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

    auto const result = applyGuts(sb);
    if (result.second)
        sb.apply(ctx_.rawView());

    return result.first;
}

std::pair<TER, STAmount>
AMMDeposit::deposit(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& amountBalance,
    STAmount const& amountDeposit,
    std::optional<STAmount> const& amount2Deposit,
    STAmount const& lptAMMBalance,
    STAmount const& lpTokensDeposit,
    std::optional<STAmount> const& depositMin,
    std::optional<STAmount> const& deposit2Min,
    std::optional<STAmount> const& lpTokensDepositMin,
    std::uint16_t tfee)
{
    // Check account has sufficient funds.
    // Return true if it does, false otherwise.
    auto checkBalance = [&](auto const& depositAmount) -> TER {
        if (depositAmount <= beast::zero)
            return temBAD_AMOUNT;
        if (isXRP(depositAmount))
        {
            auto const& lpIssue = lpTokensDeposit.issue();
            // Adjust the reserve if LP doesn't have LPToken trustline
            auto const sle = view.read(
                keylet::line(account_, lpIssue.account, lpIssue.currency));
            if (xrpLiquid(view, account_, !sle, j_) >= depositAmount)
                return tesSUCCESS;
        }
        else if (
            account_ == depositAmount.issue().account ||
            accountHolds(
                view,
                account_,
                depositAmount.issue(),
                FreezeHandling::fhIGNORE_FREEZE,
                ctx_.journal) >= depositAmount)
            return tesSUCCESS;
        return tecUNFUNDED_AMM;
    };

    auto const
        [amountDepositActual, amount2DepositActual, lpTokensDepositActual] =
            adjustAmountsByLPTokens(
                amountBalance,
                amountDeposit,
                amount2Deposit,
                lptAMMBalance,
                lpTokensDeposit,
                tfee,
                true);

    if (lpTokensDepositActual <= beast::zero)
    {
        JLOG(ctx_.journal.debug()) << "AMM Deposit: adjusted tokens zero";
        return {tecAMM_INVALID_TOKENS, STAmount{}};
    }

    if (amountDepositActual < depositMin ||
        amount2DepositActual < deposit2Min ||
        lpTokensDepositActual < lpTokensDepositMin)
    {
        JLOG(ctx_.journal.debug())
            << "AMM Deposit: min deposit fails " << amountDepositActual << " "
            << depositMin.value_or(STAmount{}) << " "
            << amount2DepositActual.value_or(STAmount{}) << " "
            << deposit2Min.value_or(STAmount{}) << " " << lpTokensDepositActual
            << " " << lpTokensDepositMin.value_or(STAmount{});
        return {tecAMM_FAILED, STAmount{}};
    }

    // Deposit amountDeposit
    if (auto const ter = checkBalance(amountDepositActual))
    {
        JLOG(ctx_.journal.debug()) << "AMM Deposit: account has insufficient "
                                      "checkBalance to deposit or is 0"
                                   << amountDepositActual;
        return {ter, STAmount{}};
    }

    auto res = accountSend(
        view,
        account_,
        ammAccount,
        amountDepositActual,
        ctx_.journal,
        WaiveTransferFee::Yes);
    if (res != tesSUCCESS)
    {
        JLOG(ctx_.journal.debug())
            << "AMM Deposit: failed to deposit " << amountDepositActual;
        return {res, STAmount{}};
    }

    // Deposit amount2Deposit
    if (amount2DepositActual)
    {
        if (auto const ter = checkBalance(*amount2DepositActual))
        {
            JLOG(ctx_.journal.debug())
                << "AMM Deposit: account has insufficient checkBalance to "
                   "deposit or is 0 "
                << *amount2DepositActual;
            return {ter, STAmount{}};
        }

        res = accountSend(
            view,
            account_,
            ammAccount,
            *amount2DepositActual,
            ctx_.journal,
            WaiveTransferFee::Yes);
        if (res != tesSUCCESS)
        {
            JLOG(ctx_.journal.debug())
                << "AMM Deposit: failed to deposit " << *amount2DepositActual;
            return {res, STAmount{}};
        }
    }

    // Deposit LP tokens
    res = accountSend(
        view, ammAccount, account_, lpTokensDepositActual, ctx_.journal);
    if (res != tesSUCCESS)
    {
        JLOG(ctx_.journal.debug()) << "AMM Deposit: failed to deposit LPTokens";
        return {res, STAmount{}};
    }

    return {tesSUCCESS, lptAMMBalance + lpTokensDepositActual};
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
    STAmount const& lpTokensDeposit,
    std::optional<STAmount> const& depositMin,
    std::optional<STAmount> const& deposit2Min,
    std::uint16_t tfee)
{
    try
    {
        auto const frac =
            divide(lpTokensDeposit, lptAMMBalance, lptAMMBalance.issue());
        return deposit(
            view,
            ammAccount,
            amountBalance,
            multiply(amountBalance, frac, amountBalance.issue()),
            multiply(amount2Balance, frac, amount2Balance.issue()),
            lptAMMBalance,
            lpTokensDeposit,
            depositMin,
            deposit2Min,
            std::nullopt,
            tfee);
    }
    catch (std::exception const& e)
    {
        // LCOV_EXCL_START
        JLOG(j_.error()) << "AMMDeposit::equalDepositTokens exception "
                         << e.what();
        return {tecINTERNAL, STAmount{}};
        // LCOV_EXCL_STOP
    }
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
 * Use equation 1 to compute the amount of t, given the amount in Asset1In.
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
    STAmount const& amount2,
    std::optional<STAmount> const& lpTokensDepositMin,
    std::uint16_t tfee)
{
    auto frac = Number{amount} / amountBalance;
    auto tokens = toSTAmount(lptAMMBalance.issue(), lptAMMBalance * frac);
    if (tokens == beast::zero)
        return {tecAMM_FAILED, STAmount{}};
    auto const amount2Deposit = amount2Balance * frac;
    if (amount2Deposit <= amount2)
        return deposit(
            view,
            ammAccount,
            amountBalance,
            amount,
            toSTAmount(amount2Balance.issue(), amount2Deposit),
            lptAMMBalance,
            tokens,
            std::nullopt,
            std::nullopt,
            lpTokensDepositMin,
            tfee);
    frac = Number{amount2} / amount2Balance;
    tokens = toSTAmount(lptAMMBalance.issue(), lptAMMBalance * frac);
    if (tokens == beast::zero)
        return {tecAMM_FAILED, STAmount{}};
    auto const amountDeposit = amountBalance * frac;
    if (amountDeposit <= amount)
        return deposit(
            view,
            ammAccount,
            amountBalance,
            toSTAmount(amountBalance.issue(), amountDeposit),
            amount2,
            lptAMMBalance,
            tokens,
            std::nullopt,
            std::nullopt,
            lpTokensDepositMin,
            tfee);
    return {tecAMM_FAILED, STAmount{}};
}

/** Single asset deposit of the amount of asset specified by Asset1In.
 *       t = T * (b / B - x) / (1 + x) (3)
 *      where
 *         f1 = (1 - 0.5 * tfee) / (1 - tfee)
 *         x = sqrt(f1**2 + b / (B * (1 - tfee)) - f1
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
    std::optional<STAmount> const& lpTokensDepositMin,
    std::uint16_t tfee)
{
    auto const tokens = lpTokensIn(amountBalance, amount, lptAMMBalance, tfee);
    if (tokens == beast::zero)
        return {tecAMM_FAILED, STAmount{}};
    return deposit(
        view,
        ammAccount,
        amountBalance,
        amount,
        std::nullopt,
        lptAMMBalance,
        tokens,
        std::nullopt,
        std::nullopt,
        lpTokensDepositMin,
        tfee);
}

/** Single asset asset1 is deposited to obtain some share of
 * the AMM instance's pools represented by amount of LPTokens.
 * Use equation 4 to compute the amount of asset1 to be deposited,
 * given t represented by amount of LPTokens. Equation 4 solves
 * equation 3 @see singleDeposit for b. Fail if b exceeds specified
 * Max amount to deposit.
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
        ammAssetIn(amountBalance, lptAMMBalance, lpTokensDeposit, tfee);
    if (amountDeposit > amount)
        return {tecAMM_FAILED, STAmount{}};
    return deposit(
        view,
        ammAccount,
        amountBalance,
        amountDeposit,
        std::nullopt,
        lptAMMBalance,
        lpTokensDeposit,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        tfee);
}

/** Single asset deposit with two constraints.
 * a. Amount of asset1 if specified (not 0) in Asset1In specifies the maximum
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
        if (tokens <= beast::zero)
            return {tecAMM_FAILED, STAmount{}};
        auto const ep = Number{amount} / tokens;
        if (ep <= ePrice)
            return deposit(
                view,
                ammAccount,
                amountBalance,
                amount,
                std::nullopt,
                lptAMMBalance,
                tokens,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                tfee);
    }

    // LPTokens is asset out => E = b / t
    // substituting t in formula (3) as b/E:
    // b/E = T * [b/B - sqrt(t2**2 + b/(f1*B)) + t2]/
    //                      [1 + sqrt(t2**2 + b/(f1*B)) -t2] (A)
    // where f1 = 1 - fee, f2 = (1 - fee/2)/f1
    // Let R = b/(f1*B), then b/B = f1*R and b = R*f1*B
    // Then (A) is
    // R*f1*B = E*T*[R*f1 -sqrt(f2**2 + R) + f2]/[1 + sqrt(f2**2 + R) - f2] =>
    // Let c = f1*B/(E*T) =>
    // R*c*(1 + sqrt(f2**2 + R) + f2) = R*f1 - sqrt(f2**2 + R) - f2 =>
    // (R*c + 1)*sqrt(f2**2 + R) = R*(f1 + c*f2 - c) + f2 =>
    // Let d = f1 + c*f2 - c =>
    // (R*c + 1)*sqrt(f2**2 + R) = R*d + f2 =>
    // (R*c + 1)**2 * (f2**2 + R) = (R*d + f2)**2 =>
    // (R*c)**2 + R*((c*f2)**2 + 2*c - d**2) + 2*c*f2**2 + 1 -2*d*f2 = 0 =>
    // a1 = c**2, b1 = (c*f2)**2 + 2*c - d**2, c1 = 2*c*f2**2 + 1 - 2*d*f2
    // R = (-b1 + sqrt(b1**2 + 4*a1*c1))/(2*a1)
    auto const f1 = feeMult(tfee);
    auto const f2 = feeMultHalf(tfee) / f1;
    auto const c = f1 * amountBalance / (ePrice * lptAMMBalance);
    auto const d = f1 + c * f2 - c;
    auto const a1 = c * c;
    auto const b1 = c * c * f2 * f2 + 2 * c - d * d;
    auto const c1 = 2 * c * f2 * f2 + 1 - 2 * d * f2;
    auto const amountDeposit = toSTAmount(
        amountBalance.issue(),
        f1 * amountBalance * solveQuadraticEq(a1, b1, c1));
    if (amountDeposit <= beast::zero)
        return {tecAMM_FAILED, STAmount{}};
    auto const tokens =
        toSTAmount(lptAMMBalance.issue(), amountDeposit / ePrice);
    return deposit(
        view,
        ammAccount,
        amountBalance,
        amountDeposit,
        std::nullopt,
        lptAMMBalance,
        tokens,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        tfee);
}

std::pair<TER, STAmount>
AMMDeposit::equalDepositInEmptyState(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& amount,
    STAmount const& amount2,
    Issue const& lptIssue,
    std::uint16_t tfee)
{
    return deposit(
        view,
        ammAccount,
        amount,
        amount,
        amount2,
        STAmount{lptIssue, 0},
        ammLPTokens(amount, amount2, lptIssue),
        std::nullopt,
        std::nullopt,
        std::nullopt,
        tfee);
}

}  // namespace ripple
