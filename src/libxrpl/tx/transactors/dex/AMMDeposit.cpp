/** @file AMMDeposit.cpp
 *  Implementation of the AMMDeposit transactor (XLS-30d).
 *
 *  Allows liquidity providers to deposit one or both assets into an AMM pool
 *  and receive LP tokens representing their proportional ownership. Six deposit
 *  modes are supported, each selected by a single flag bit from `tfDepositSubTx`.
 *  See the class-level documentation in `AMMDeposit.h` for the mode table and
 *  the high-level invariants that govern all paths.
 *
 *  @note This file implements the mathematical core of each deposit mode.
 *      Equations are numbered to match the XLS-30d specification.
 */
#include <xrpl/tx/transactors/dex/AMMDeposit.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/AMMHelpers.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <bit>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <utility>

namespace xrpl {

bool
AMMDeposit::checkExtraFeatures(PreflightContext const& ctx)
{
    if (!ammEnabled(ctx.rules))
        return false;

    auto const amount = ctx.tx[~sfAmount];
    auto const amount2 = ctx.tx[~sfAmount2];

    return ctx.rules.enabled(featureMPTokensV2) ||
        (!ctx.tx[sfAsset].holds<MPTIssue>() && !ctx.tx[sfAsset2].holds<MPTIssue>() &&
         !(amount && amount->holds<MPTIssue>()) && !(amount2 && amount2->holds<MPTIssue>()));
}

std::uint32_t
AMMDeposit::getFlagsMask(PreflightContext const& ctx)
{
    return tfAMMDepositMask;
}

NotTEC
AMMDeposit::preflight(PreflightContext const& ctx)
{
    auto const flags = ctx.tx.getFlags();
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
    if ((flags & tfLPToken) != 0u)
    {
        // if included then both amount and amount2 are deposit min
        if (!lpTokens || ePrice || (amount && !amount2) || (!amount && amount2) || tradingFee)
            return temMALFORMED;
    }
    else if ((flags & tfSingleAsset) != 0u)
    {
        // if included then lpTokens is deposit min
        if (!amount || amount2 || ePrice || tradingFee)
            return temMALFORMED;
    }
    else if ((flags & tfTwoAsset) != 0u)
    {
        // if included then lpTokens is deposit min
        if (!amount || !amount2 || ePrice || tradingFee)
            return temMALFORMED;
    }
    else if ((flags & tfOneAssetLPToken) != 0u)
    {
        if (!amount || !lpTokens || amount2 || ePrice || tradingFee)
            return temMALFORMED;
    }
    else if ((flags & tfLimitLPToken) != 0u)
    {
        if (!amount || !ePrice || lpTokens || amount2 || tradingFee)
            return temMALFORMED;
    }
    else if ((flags & tfTwoAssetIfEmpty) != 0u)
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

    if (amount && amount2 && amount->asset() == amount2->asset())
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: invalid tokens, same issue." << amount->asset() << " "
                            << amount2->asset();
        return temBAD_AMM_TOKENS;
    }

    if (lpTokens && *lpTokens <= beast::kZERO)
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: invalid LPTokens";
        return temBAD_AMM_TOKENS;
    }

    if (amount)
    {
        if (auto const res = invalidAMMAmount(
                *amount, std::make_optional(std::make_pair(asset, asset2)), ePrice.has_value()))
        {
            JLOG(ctx.j.debug()) << "AMM Deposit: invalid amount";
            return res;
        }
    }

    if (amount2)
    {
        if (auto const res =
                invalidAMMAmount(*amount2, std::make_optional(std::make_pair(asset, asset2))))
        {
            JLOG(ctx.j.debug()) << "AMM Deposit: invalid amount2";
            return res;
        }
    }

    if (amount && ePrice)
    {
        auto assets = [&]() -> std::optional<std::pair<Asset, Asset>> {
            // don't check ePrice issue
            if (ctx.rules.enabled(featureMPTokensV2))
                return std::nullopt;
            // must be amount issue
            return std::make_optional(std::make_pair(amount->asset(), amount->asset()));
        }();
        if (auto const res = invalidAMMAmount(*ePrice, assets))
        {
            JLOG(ctx.j.debug()) << "AMM Deposit: invalid EPrice";
            return res;
        }
    }

    if (tradingFee > kTRADING_FEE_THRESHOLD)
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: invalid trading fee.";
        return temBAD_FEE;
    }

    return tesSUCCESS;
}

TER
AMMDeposit::preclaim(PreclaimContext const& ctx)
{
    auto const accountID = ctx.tx[sfAccount];

    auto const ammSle = ctx.view.read(keylet::amm(ctx.tx[sfAsset], ctx.tx[sfAsset2]));
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
        FreezeHandling::IgnoreFreeze,
        AuthHandling::IgnoreAuth,
        ctx.j);
    if (!expected)
        return expected.error();  // LCOV_EXCL_LINE
    auto const [amountBalance, amount2Balance, lptAMMBalance] = *expected;
    if ((ctx.tx.getFlags() & tfTwoAssetIfEmpty) != 0u)
    {
        if (lptAMMBalance != beast::kZERO)
            return tecAMM_NOT_EMPTY;
        if (amountBalance != beast::kZERO || amount2Balance != beast::kZERO)
        {
            // LCOV_EXCL_START
            JLOG(ctx.j.debug()) << "AMM Deposit: tokens balance is not zero.";
            return tecINTERNAL;
            // LCOV_EXCL_STOP
        }
    }
    else
    {
        if (lptAMMBalance == beast::kZERO)
            return tecAMM_EMPTY;
        if (amountBalance <= beast::kZERO || amount2Balance <= beast::kZERO ||
            lptAMMBalance < beast::kZERO)
        {
            // LCOV_EXCL_START
            JLOG(ctx.j.debug()) << "AMM Deposit: reserves or tokens balance is zero.";
            return tecINTERNAL;
            // LCOV_EXCL_STOP
        }
    }

    // Balance check is re-run inside deposit() for modes where amounts are
    // derived from pool math rather than stated in the transaction directly.
    auto balance = [&](auto const& deposit) -> TER {
        if (isXRP(deposit))
        {
            auto const lpIssue = (*ammSle)[sfLPTokenBalance].get<Issue>();
            // Adjust the reserve if LP doesn't have LPToken trustline
            auto const sle =
                ctx.view.read(keylet::line(accountID, lpIssue.account, lpIssue.currency));
            if (xrpLiquid(ctx.view, accountID, !sle, ctx.j) >= deposit)
                return TER(tesSUCCESS);
            if (sle)
                return tecUNFUNDED_AMM;
            return tecINSUF_RESERVE_LINE;
        }
        return accountFunds(
                   ctx.view,
                   accountID,
                   deposit,
                   FreezeHandling::IgnoreFreeze,
                   AuthHandling::IgnoreAuth,
                   ctx.j) >= deposit
            ? TER(tesSUCCESS)
            : tecUNFUNDED_AMM;
    };

    if (ctx.view.rules().enabled(featureAMMClawback))
    {
        // Check if either of the assets is frozen, AMMDeposit is not allowed
        // if either asset is frozen
        auto checkAsset = [&](Asset const& asset) -> TER {
            // WeakAuth - don't need to check if MPT object exists as might be
            // depositing into non-MPT pool. It'll fail on send if MPT doesn't
            // exist.
            if (auto const ter = requireAuth(ctx.view, asset, accountID, AuthType::WeakAuth))
            {
                JLOG(ctx.j.debug()) << "AMM Deposit: account is not authorized, " << asset;
                return ter;
            }

            if (isFrozen(ctx.view, accountID, asset))
            {
                JLOG(ctx.j.debug()) << "AMM Deposit: account or currency is frozen, "
                                    << to_string(accountID) << " " << to_string(asset);

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

    auto checkAmount = [&](std::optional<STAmount> const& amount, bool checkBalance) -> TER {
        if (amount)
        {
            // This normally should not happen.
            // Account is not authorized to hold the assets it's depositing,
            // or it doesn't even have a trust line or MPT for them.
            if (auto const ter = requireAuth(ctx.view, amount->asset(), accountID))
            {
                // LCOV_EXCL_START
                JLOG(ctx.j.debug())
                    << "AMM Deposit: account is not authorized, " << amount->asset();
                return ter;
                // LCOV_EXCL_STOP
            }
            // AMM account or currency frozen
            if (isFrozen(ctx.view, ammAccountID, amount->asset()))
            {
                JLOG(ctx.j.debug())
                    << "AMM Deposit: AMM account or currency is frozen, " << to_string(accountID);
                return tecFROZEN;
            }
            // Account frozen
            if (isIndividualFrozen(ctx.view, accountID, amount->asset()))
            {
                JLOG(ctx.j.debug()) << "AMM Deposit: account is frozen, " << to_string(accountID)
                                    << " " << to_string(amount->asset());
                return tecFROZEN;
            }
            if (checkBalance)
            {
                if (auto const ter = balance(*amount))
                {
                    JLOG(ctx.j.debug())
                        << "AMM Deposit: account has insufficient funds, " << *amount;
                    return ter;
                }
            }
        }
        return tesSUCCESS;
    };

    // amount and amount2 are deposit min in case of tfLPToken
    if ((ctx.tx.getFlags() & tfLPToken) == 0u)
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

    if (auto const lpTokens = ctx.tx[~sfLPTokenOut];
        lpTokens && lpTokens->asset() != lptAMMBalance.asset())
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: invalid LPTokens.";
        return temBAD_AMM_TOKENS;
    }

    // Check the reserve for LPToken trustline if not LP.
    // We checked above but need to check again if depositing IOU only.
    if (ammLPHolds(ctx.view, *ammSle, accountID, ctx.j) == beast::kZERO)
    {
        STAmount const xrpBalance = xrpLiquid(ctx.view, accountID, 1, ctx.j);
        if (xrpBalance <= beast::kZERO)
        {
            JLOG(ctx.j.debug()) << "AMM Instance: insufficient reserves";
            return tecINSUF_RESERVE_LINE;
        }
    }

    if (auto const ter = checkMPTTxAllowed(ctx.view, ttAMM_DEPOSIT, ctx.tx[sfAsset], accountID);
        !isTesSuccess(ter))
        return ter;
    if (auto const ter = checkMPTTxAllowed(ctx.view, ttAMM_DEPOSIT, ctx.tx[sfAsset2], accountID);
        !isTesSuccess(ter))
        return ter;

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
        amount ? amount->asset() : std::optional<Asset>{},
        amount2 ? amount2->asset() : std::optional<Asset>{},
        FreezeHandling::ZeroIfFrozen,
        AuthHandling::ZeroIfUnauthorized,
        ctx_.journal);
    if (!expected)
        return {expected.error(), false};  // LCOV_EXCL_LINE
    auto const [amountBalance, amount2Balance, lptAMMBalance] = *expected;
    auto const tfee = (lptAMMBalance == beast::kZERO)
        ? ctx_.tx[~sfTradingFee].value_or(0)
        : getTradingFee(ctx_.view(), *ammSle, account_);

    auto const subTxType = ctx_.tx.getFlags() & tfDepositSubTx;

    auto const [result, newLPTokenBalance] = [&,
                                              &amountBalance = amountBalance,
                                              &amount2Balance = amount2Balance,
                                              &lptAMMBalance =
                                                  lptAMMBalance]() -> std::pair<TER, STAmount> {
        if (subTxType & tfTwoAsset)
        {
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
        }
        if (subTxType & tfOneAssetLPToken)
        {
            return singleDepositTokens(
                sb, ammAccountID, amountBalance, *amount, lptAMMBalance, *lpTokensDeposit, tfee);
        }
        if (subTxType & tfLimitLPToken)
        {
            return singleDepositEPrice(
                sb, ammAccountID, amountBalance, *amount, lptAMMBalance, *ePrice, tfee);
        }
        if (subTxType & tfSingleAsset)
        {
            return singleDeposit(
                sb, ammAccountID, amountBalance, lptAMMBalance, *amount, lpTokensDeposit, tfee);
        }
        if (subTxType & tfLPToken)
        {
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
        }
        if (subTxType & tfTwoAssetIfEmpty)
        {
            return equalDepositInEmptyState(
                sb, ammAccountID, *amount, *amount2, lptAMMBalance.asset(), tfee);
        }
        // should not happen.
        // LCOV_EXCL_START
        JLOG(j_.error()) << "AMM Deposit: invalid options.";
        return std::make_pair(tecINTERNAL, STAmount{});
        // LCOV_EXCL_STOP
    }();

    if (isTesSuccess(result))
    {
        XRPL_ASSERT(
            newLPTokenBalance > beast::kZERO,
            "xrpl::AMMDeposit::applyGuts : valid new LP token balance");
        ammSle->setFieldAmount(sfLPTokenBalance, newLPTokenBalance);
        // LP depositing into AMM empty state gets the auction slot
        // and the voting
        if (lptAMMBalance == beast::kZERO)
            initializeFeeAuctionVote(sb, ammSle, account_, lptAMMBalance.asset(), tfee);

        sb.update(ammSle);
    }

    return {result, isTesSuccess(result)};
}

TER
AMMDeposit::doApply()
{
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
    auto checkBalance = [&](auto const& depositAmount) -> TER {
        if (depositAmount <= beast::kZERO)
            return temBAD_AMOUNT;
        if (isXRP(depositAmount))
        {
            auto const& lpIssue = lpTokensDeposit.get<Issue>();
            // Adjust the reserve if LP doesn't have LPToken trustline
            auto const sle = view.read(keylet::line(account_, lpIssue.account, lpIssue.currency));
            if (xrpLiquid(view, account_, !sle, j_) >= depositAmount)
                return tesSUCCESS;
        }
        else if (
            accountFunds(
                view,
                account_,
                depositAmount,
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                ctx_.journal) >= depositAmount)
        {
            return tesSUCCESS;
        }
        return tecUNFUNDED_AMM;
    };

    auto const [amountDepositActual, amount2DepositActual, lpTokensDepositActual] =
        adjustAmountsByLPTokens(
            amountBalance,
            amountDeposit,
            amount2Deposit,
            lptAMMBalance,
            lpTokensDeposit,
            tfee,
            IsDeposit::Yes);

    if (lpTokensDepositActual <= beast::kZERO)
    {
        JLOG(ctx_.journal.debug()) << "AMM Deposit: adjusted tokens zero";
        return {tecAMM_INVALID_TOKENS, STAmount{}};
    }

    if (amountDepositActual < depositMin || amount2DepositActual < deposit2Min ||
        lpTokensDepositActual < lpTokensDepositMin)
    {
        JLOG(ctx_.journal.debug())
            << "AMM Deposit: min deposit fails " << amountDepositActual << " "
            << depositMin.value_or(STAmount{}) << " " << amount2DepositActual.value_or(STAmount{})
            << " " << deposit2Min.value_or(STAmount{}) << " " << lpTokensDepositActual << " "
            << lpTokensDepositMin.value_or(STAmount{});
        return {tecAMM_FAILED, STAmount{}};
    }

    if (auto const ter = checkBalance(amountDepositActual))
    {
        JLOG(ctx_.journal.debug()) << "AMM Deposit: account has insufficient "
                                      "checkBalance to deposit or is 0"
                                   << amountDepositActual;
        return {ter, STAmount{}};
    }

    auto res = accountSend(
        view, account_, ammAccount, amountDepositActual, ctx_.journal, WaiveTransferFee::Yes);
    if (!isTesSuccess(res))
    {
        JLOG(ctx_.journal.debug()) << "AMM Deposit: failed to deposit " << amountDepositActual;
        return {res, STAmount{}};
    }

    if (amount2DepositActual)
    {
        if (auto const ter = checkBalance(*amount2DepositActual))
        {
            JLOG(ctx_.journal.debug()) << "AMM Deposit: account has insufficient checkBalance to "
                                          "deposit or is 0 "
                                       << *amount2DepositActual;
            return {ter, STAmount{}};
        }

        res = accountSend(
            view, account_, ammAccount, *amount2DepositActual, ctx_.journal, WaiveTransferFee::Yes);
        if (!isTesSuccess(res))
        {
            JLOG(ctx_.journal.debug())
                << "AMM Deposit: failed to deposit " << *amount2DepositActual;
            return {res, STAmount{}};
        }
    }

    res = accountSend(view, ammAccount, account_, lpTokensDepositActual, ctx_.journal);
    if (!isTesSuccess(res))
    {
        JLOG(ctx_.journal.debug()) << "AMM Deposit: failed to deposit LPTokens";
        return {res, STAmount{}};
    }

    return {tesSUCCESS, lptAMMBalance + lpTokensDepositActual};
}

/** Apply `fixAMMv1_3` rounding to a prospective LP token output amount.
 *
 *  Without the fix, deposit-mode helpers used their raw computed token value
 *  directly, which could produce a non-zero token count that rounds to zero
 *  after the integer precision step in `adjustAmountsByLPTokens`. The fix
 *  pre-rounds via `adjustLPTokens` so callers can detect the collapse early
 *  and return `tecAMM_INVALID_TOKENS` instead of silently failing with
 *  `tecAMM_FAILED`.
 *
 *  @param rules           Current ledger rules (checked for `fixAMMv1_3`).
 *  @param lptAMMBalance   Current total LP token supply (used for rounding context).
 *  @param lpTokensDeposit Computed LP token output before adjustment.
 *  @return The adjusted token amount; equals `lpTokensDeposit` unchanged if
 *          `fixAMMv1_3` is not active.
 */
static STAmount
adjustLPTokensOut(
    Rules const& rules,
    STAmount const& lptAMMBalance,
    STAmount const& lpTokensDeposit)
{
    if (!rules.enabled(fixAMMv1_3))
        return lpTokensDeposit;
    return adjustLPTokens(lptAMMBalance, lpTokensDeposit, IsDeposit::Yes);
}

/** `tfLPToken` deposit: proportional two-asset deposit for a targeted LP token amount.
 *
 *  Computes the required deposit of each asset as:
 *  @code
 *    amountDeposit  = amountBalance  * (tokensAdj / lptAMMBalance)
 *    amount2Deposit = amount2Balance * (tokensAdj / lptAMMBalance)
 *  @endcode
 *  where `tokensAdj` is `lpTokensDeposit` after `adjustLPTokensOut` pre-rounding.
 *  The optional `depositMin` / `deposit2Min` fields from the transaction act as
 *  minimum thresholds, checked inside `deposit()`. No trading fee is charged
 *  because the deposit is perfectly proportional to the pool's current composition.
 *
 *  @note Under `fixAMMv1_3`, if `tokensAdj` rounds to zero the function returns
 *      `tecAMM_INVALID_TOKENS` immediately, before calling `deposit()`.
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
        auto const tokensAdj = adjustLPTokensOut(view.rules(), lptAMMBalance, lpTokensDeposit);
        if (view.rules().enabled(fixAMMv1_3) && tokensAdj == beast::kZERO)
            return {tecAMM_INVALID_TOKENS, STAmount{}};
        auto const frac = divide(tokensAdj, lptAMMBalance, lptAMMBalance.asset());
        // amounts factor in the adjusted tokens
        auto const amountDeposit =
            getRoundedAsset(view.rules(), amountBalance, frac, IsDeposit::Yes);
        auto const amount2Deposit =
            getRoundedAsset(view.rules(), amount2Balance, frac, IsDeposit::Yes);
        return deposit(
            view,
            ammAccount,
            amountBalance,
            amountDeposit,
            amount2Deposit,
            lptAMMBalance,
            tokensAdj,
            depositMin,
            deposit2Min,
            std::nullopt,
            tfee);
    }
    catch (std::exception const& e)
    {
        // LCOV_EXCL_START
        JLOG(j_.error()) << "AMMDeposit::equalDepositTokens exception " << e.what();
        return {tecINTERNAL, STAmount{}};
        // LCOV_EXCL_STOP
    }
}

/** `tfTwoAsset` deposit: proportional two-asset deposit bounded by per-asset maximums.
 *
 *  Uses the proportional deposit equations:
 *  @code
 *    a = (t/T) * A        (1)
 *    b = (t/T) * B        (2)
 *  @endcode
 *  where A, B are pool reserves, T is current LP supply, and t is LP tokens issued.
 *
 *  Algorithm:
 *  1. Derive t from equation (1) using `amount` (asset1 max) → Z.
 *  2. Derive required asset2 from equation (2) using Z → X.
 *  3. If X ≤ `amount2`: deposit (`amount`, X, Z).
 *  4. Otherwise re-derive t from equation (2) using `amount2` → W,
 *     then asset1 from equation (1) using W → Y.
 *  5. If Y ≤ `amount`: deposit (Y, `amount2`, W).
 *  6. If neither direction satisfies both bounds simultaneously: `tecAMM_FAILED`.
 *
 *  Under `fixAMMv1_3`, a zero token result at step 1 or 4 returns
 *  `tecAMM_INVALID_TOKENS` instead of `tecAMM_FAILED`.
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
    auto tokensAdj = getRoundedLPTokens(view.rules(), lptAMMBalance, frac, IsDeposit::Yes);
    if (tokensAdj == beast::kZERO)
    {
        if (!view.rules().enabled(fixAMMv1_3))
        {
            return {tecAMM_FAILED, STAmount{}};  // LCOV_EXCL_LINE
        }

        return {tecAMM_INVALID_TOKENS, STAmount{}};
    }
    // factor in the adjusted tokens
    frac = adjustFracByTokens(view.rules(), lptAMMBalance, tokensAdj, frac);
    auto const amount2Deposit = getRoundedAsset(view.rules(), amount2Balance, frac, IsDeposit::Yes);
    if (amount2Deposit <= amount2)
    {
        return deposit(
            view,
            ammAccount,
            amountBalance,
            amount,
            amount2Deposit,
            lptAMMBalance,
            tokensAdj,
            std::nullopt,
            std::nullopt,
            lpTokensDepositMin,
            tfee);
    }
    frac = Number{amount2} / amount2Balance;
    tokensAdj = getRoundedLPTokens(view.rules(), lptAMMBalance, frac, IsDeposit::Yes);
    if (tokensAdj == beast::kZERO)
    {
        if (!view.rules().enabled(fixAMMv1_3))
        {
            return {tecAMM_FAILED, STAmount{}};  // LCOV_EXCL_LINE
        }

        return {tecAMM_INVALID_TOKENS, STAmount{}};  // LCOV_EXCL_LINE
    }
    // factor in the adjusted tokens
    frac = adjustFracByTokens(view.rules(), lptAMMBalance, tokensAdj, frac);
    auto const amountDeposit = getRoundedAsset(view.rules(), amountBalance, frac, IsDeposit::Yes);
    if (amountDeposit <= amount)
    {
        return deposit(
            view,
            ammAccount,
            amountBalance,
            amountDeposit,
            amount2,
            lptAMMBalance,
            tokensAdj,
            std::nullopt,
            std::nullopt,
            lpTokensDepositMin,
            tfee);
    }
    return {tecAMM_FAILED, STAmount{}};
}

/** `tfSingleAsset` deposit: single-asset deposit for a specified asset amount.
 *
 *  Computes LP tokens issued via the single-deposit formula (XLS-30 eq. 3):
 *  @code
 *    t = T * (b/B - x) / (1 + x)
 *  @endcode
 *  where `x = sqrt(f1² + b / (B * (1-fee))) - f1` and
 *  `f1 = (1 - 0.5*fee) / (1 - fee)`. The trading fee is charged because
 *  depositing a single asset is equivalent to an implicit swap of half the
 *  input for the other asset, followed by a proportional deposit.
 *
 *  After computing tokens via `lpTokensOut`, `adjustLPTokensOut` is applied
 *  for `fixAMMv1_3` pre-rounding, and `adjustAssetInByTokens` back-derives the
 *  exact asset input consistent with the rounded token count.
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
    auto const tokens = adjustLPTokensOut(
        view.rules(), lptAMMBalance, lpTokensOut(amountBalance, amount, lptAMMBalance, tfee));
    if (tokens == beast::kZERO)
    {
        if (!view.rules().enabled(fixAMMv1_3))
        {
            return {tecAMM_FAILED, STAmount{}};  // LCOV_EXCL_LINE
        }

        return {tecAMM_INVALID_TOKENS, STAmount{}};
    }
    // factor in the adjusted tokens
    auto const [tokensAdj, amountDepositAdj] =
        adjustAssetInByTokens(view.rules(), amountBalance, amount, lptAMMBalance, tokens, tfee);
    if (view.rules().enabled(fixAMMv1_3) && tokensAdj == beast::kZERO)
        return {tecAMM_INVALID_TOKENS, STAmount{}};  // LCOV_EXCL_LINE
    return deposit(
        view,
        ammAccount,
        amountBalance,
        amountDepositAdj,
        std::nullopt,
        lptAMMBalance,
        tokensAdj,
        std::nullopt,
        std::nullopt,
        lpTokensDepositMin,
        tfee);
}

/** `tfOneAssetLPToken` deposit: single-asset deposit targeting a specific LP token output.
 *
 *  Inverts the single-deposit formula (XLS-30 eq. 4, solving eq. 3 for `b`) to
 *  derive the required asset1 input given the desired `lpTokensDeposit`. If the
 *  computed asset1 input exceeds the caller's stated maximum (`amount`), the
 *  transaction fails with `tecAMM_FAILED`. The same trading fee rationale as
 *  `singleDeposit` applies.
 *
 *  `adjustLPTokensOut` is applied before the inverse calculation so that
 *  `tokensAdj` represents the same integer-rounded value that would actually be
 *  issued, keeping asset input and token output mutually consistent.
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
    auto const tokensAdj = adjustLPTokensOut(view.rules(), lptAMMBalance, lpTokensDeposit);
    if (view.rules().enabled(fixAMMv1_3) && tokensAdj == beast::kZERO)
        return {tecAMM_INVALID_TOKENS, STAmount{}};
    // the adjusted tokens are factored in
    auto const amountDeposit = ammAssetIn(amountBalance, lptAMMBalance, tokensAdj, tfee);
    if (amountDeposit > amount)
        return {tecAMM_FAILED, STAmount{}};
    return deposit(
        view,
        ammAccount,
        amountBalance,
        amountDeposit,
        std::nullopt,
        lptAMMBalance,
        tokensAdj,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        tfee);
}

/** `tfLimitLPToken` deposit: single-asset deposit with an effective-price ceiling.
 *
 *  Enforces the constraint that the effective price EP = asset1_in / LP_out must
 *  not exceed `ePrice`. Two-pass algorithm:
 *
 *  **Pass 1** (only when `amount != 0`): compute LP tokens from `amount` via
 *  `lpTokensOut` (eq. 3). If EP = `amount / tokens` ≤ `ePrice`, deposit that.
 *
 *  **Pass 2** (when pass 1 is skipped or EP exceeds `ePrice`): solve for the
 *  exact asset1 and LP token quantities at EP = `ePrice` using `solveQuadraticEq`.
 *  The derivation substituting the effective-price constraint back into eq. 3
 *  reduces to a quadratic in R = b / (f1 * B):
 *  @code
 *    a1*R² + b1*R + c1 = 0
 *    a1 = c²,  b1 = (c*f2)² + 2c − d²,  c1 = 2c*f2² + 1 − 2d*f2
 *    c = f1*B / (ePrice*T),  d = f1 + c*f2 − c
 *    f1 = 1 − fee,  f2 = (1 − fee/2) / f1
 *  @endcode
 *  The positive root gives asset1 in = R * f1 * B; LP tokens out = asset1 / ePrice.
 *  Both values are rounded via `getRoundedAsset` / `getRoundedLPTokens` before
 *  a final `adjustAssetInByTokens` step ensures internal consistency.
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
    if (amount != beast::kZERO)
    {
        auto const tokens = adjustLPTokensOut(
            view.rules(), lptAMMBalance, lpTokensOut(amountBalance, amount, lptAMMBalance, tfee));
        if (tokens <= beast::kZERO)
        {
            if (!view.rules().enabled(fixAMMv1_3))
            {
                return {tecAMM_FAILED, STAmount{}};  // LCOV_EXCL_LINE
            }

            return {tecAMM_INVALID_TOKENS, STAmount{}};
        }
        // factor in the adjusted tokens
        auto const [tokensAdj, amountDepositAdj] =
            adjustAssetInByTokens(view.rules(), amountBalance, amount, lptAMMBalance, tokens, tfee);
        if (view.rules().enabled(fixAMMv1_3) && tokensAdj == beast::kZERO)
            return {tecAMM_INVALID_TOKENS, STAmount{}};  // LCOV_EXCL_LINE
        auto const ep = Number{amountDepositAdj} / tokensAdj;
        if (ep <= ePrice)
        {
            return deposit(
                view,
                ammAccount,
                amountBalance,
                amountDepositAdj,
                std::nullopt,
                lptAMMBalance,
                tokensAdj,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                tfee);
        }
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
    auto amtNoRoundCb = [&] { return f1 * amountBalance * solveQuadraticEq(a1, b1, c1); };
    auto amtProdCb = [&] { return f1 * solveQuadraticEq(a1, b1, c1); };
    auto const amountDeposit =
        getRoundedAsset(view.rules(), amtNoRoundCb, amountBalance, amtProdCb, IsDeposit::Yes);
    if (amountDeposit <= beast::kZERO)
        return {tecAMM_FAILED, STAmount{}};
    auto tokNoRoundCb = [&] { return amountDeposit / ePrice; };
    auto tokProdCb = [&] { return amountDeposit / ePrice; };
    auto const tokens =
        getRoundedLPTokens(view.rules(), tokNoRoundCb, lptAMMBalance, tokProdCb, IsDeposit::Yes);
    // factor in the adjusted tokens
    auto const [tokensAdj, amountDepositAdj] = adjustAssetInByTokens(
        view.rules(), amountBalance, amountDeposit, lptAMMBalance, tokens, tfee);
    if (view.rules().enabled(fixAMMv1_3) && tokensAdj == beast::kZERO)
        return {tecAMM_INVALID_TOKENS, STAmount{}};  // LCOV_EXCL_LINE

    return deposit(
        view,
        ammAccount,
        amountBalance,
        amountDepositAdj,
        std::nullopt,
        lptAMMBalance,
        tokensAdj,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        tfee);
}

/** `tfTwoAssetIfEmpty` deposit: bootstrap a zero-balance AMM pool.
 *
 *  Computes the initial LP token supply as `sqrt(amount * amount2)` via
 *  `ammLPTokens` and seeds the pool by calling `deposit()` with both `amount`
 *  values serving as both the deposit quantities and the notional "pool balance"
 *  (since the pool is empty, there is no prior balance to ratio against). After
 *  `applyGuts` commits the result, `initializeFeeAuctionVote` uses `tfee` to
 *  record the bootstrapping LP's initial auction-slot and voting position.
 *
 *  @note `preclaim` enforces that this mode is only reachable when
 *      `lptAMMBalance == 0`. Any concurrent deposit that races to populate the
 *      pool first will cause this transaction to fail with `tecAMM_NOT_EMPTY`.
 */
std::pair<TER, STAmount>
AMMDeposit::equalDepositInEmptyState(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& amount,
    STAmount const& amount2,
    Asset const& lptIssue,
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

void
AMMDeposit::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
AMMDeposit::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
