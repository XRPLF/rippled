#include <xrpl/tx/transactors/dex/AMMDeposit.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/AMMCurve.h>
#include <xrpl/ledger/helpers/AMMHelpers.h>
#include <xrpl/ledger/helpers/AMMTickMath.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/transactors/token/MPTokenIssuanceCreate.h>

#include <bit>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
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
    if (ctx.tx.isFlag(tfLPToken))
    {
        // if included then both amount and amount2 are deposit min
        if (!lpTokens || ePrice || (amount && !amount2) || (!amount && amount2) || tradingFee)
            return temMALFORMED;
    }
    else if (ctx.tx.isFlag(tfSingleAsset))
    {
        // if included then lpTokens is deposit min
        if (!amount || amount2 || ePrice || tradingFee)
            return temMALFORMED;
    }
    else if (ctx.tx.isFlag(tfTwoAsset))
    {
        // if included then lpTokens is deposit min
        if (!amount || !amount2 || ePrice || tradingFee)
            return temMALFORMED;
    }
    else if (ctx.tx.isFlag(tfOneAssetLPToken))
    {
        if (!amount || !lpTokens || amount2 || ePrice || tradingFee)
            return temMALFORMED;
    }
    else if (ctx.tx.isFlag(tfLimitLPToken))
    {
        if (!amount || !ePrice || lpTokens || amount2 || tradingFee)
            return temMALFORMED;
    }
    else if (ctx.tx.isFlag(tfTwoAssetIfEmpty))
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

    if (lpTokens && *lpTokens <= beast::kZero)
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

    if (tradingFee > kTradingFeeThreshold)
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: invalid trading fee.";
        return temBAD_FEE;
    }

    auto const curveType = ctx.tx[~sfCurveType].value_or(std::uint8_t(CtConstantProduct));

    if (curveType == CtConcentratedLiquidity)
    {
        if (!ctx.rules.enabled(featureAMMCurves))
            return temDISABLED;

        // CL deposits only support tfTwoAsset and tfSingleAsset
        if ((flags & tfDepositSubTx) != tfTwoAsset && (flags & tfDepositSubTx) != tfSingleAsset)
        {
            JLOG(ctx.j.debug()) << "AMM Deposit: invalid flags for CL pool.";
            return temMALFORMED;
        }

        auto const tickLower = ctx.tx[~sfTickLower];
        auto const tickUpper = ctx.tx[~sfTickUpper];
        if (!tickLower || !tickUpper)
        {
            JLOG(ctx.j.debug()) << "AMM Deposit: tick bounds required for CL pool.";
            return temMALFORMED;
        }
        if (*tickLower >= *tickUpper)
        {
            JLOG(ctx.j.debug()) << "AMM Deposit: tickLower must be less than tickUpper.";
            return temMALFORMED;
        }
        // Global tick-range bounds can be checked here without the pool
        // SLE; per-pool alignment to sfTickSpacing is checked at apply
        // time (it depends on the pool's fee tier).
        if (*tickLower < minTick || *tickUpper > maxTick)
        {
            JLOG(ctx.j.debug()) << "AMM Deposit: tick out of global range.";
            return temMALFORMED;
        }
    }
    else if (curveType == CtBinned)
    {
        if (!ctx.rules.enabled(featureAMMCurves))
            return temDISABLED;

        // Binned deposits require tfTwoAsset (single-sided deferred).
        if ((flags & tfDepositSubTx) != tfTwoAsset)
        {
            JLOG(ctx.j.debug()) << "AMM Deposit: binned requires tfTwoAsset.";
            return temMALFORMED;
        }

        auto const binID = ctx.tx[~sfBinID];
        if (!binID)
        {
            JLOG(ctx.j.debug()) << "AMM Deposit: BinID required for binned pool.";
            return temMALFORMED;
        }
        if (*binID < minBinID || *binID > maxBinID)
        {
            JLOG(ctx.j.debug()) << "AMM Deposit: BinID out of bounds.";
            return temMALFORMED;
        }
        if (ctx.tx.isFieldPresent(sfTickLower) || ctx.tx.isFieldPresent(sfTickUpper))
        {
            JLOG(ctx.j.debug()) << "AMM Deposit: tick fields not allowed for binned pool.";
            return temMALFORMED;
        }
    }
    else
    {
        // Non-CL/non-Binned pools must not have tick or bin fields
        if (ctx.tx.isFieldPresent(sfTickLower) || ctx.tx.isFieldPresent(sfTickUpper) ||
            ctx.tx.isFieldPresent(sfBinID))
        {
            JLOG(ctx.j.debug())
                << "AMM Deposit: range/bin fields not allowed for non-CL/Binned pool.";
            return temMALFORMED;
        }
    }

    return tesSUCCESS;
}

TER
AMMDeposit::preclaim(PreclaimContext const& ctx)
{
    auto const accountID = ctx.tx[sfAccount];

    auto const curveType = ctx.tx.isFieldPresent(sfCurveType) ? ctx.tx.getFieldU8(sfCurveType)
                                                              : std::uint8_t(CtConstantProduct);
    auto const ammSle = ctx.view.read(keylet::amm(ctx.tx[sfAsset], ctx.tx[sfAsset2], curveType));
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
    if (ctx.tx.isFlag(tfTwoAssetIfEmpty))
    {
        if (lptAMMBalance != beast::kZero)
            return tecAMM_NOT_EMPTY;
        if (amountBalance != beast::kZero || amount2Balance != beast::kZero)
        {
            // LCOV_EXCL_START
            JLOG(ctx.j.debug()) << "AMM Deposit: tokens balance is not zero.";
            return tecINTERNAL;
            // LCOV_EXCL_STOP
        }
    }
    else
    {
        // CL and Binned pools have no fungible LP token supply —
        // ownership is per-position / per-bin. LPTokenBalance is always
        // zero for these curves; reserves grow from per-bin or
        // per-position deposits, not from a synthetic LP share. Skip
        // the AMM_EMPTY guard.
        if (curveType != CtConcentratedLiquidity && curveType != CtBinned)
        {
            if (lptAMMBalance == beast::kZero)
                return tecAMM_EMPTY;
            if (amountBalance <= beast::kZero || amount2Balance <= beast::kZero ||
                lptAMMBalance < beast::kZero)
            {
                // LCOV_EXCL_START
                JLOG(ctx.j.debug()) << "AMM Deposit: reserves or tokens balance is zero.";
                return tecINTERNAL;
                // LCOV_EXCL_STOP
            }
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
            auto const lpIssue = (*ammSle)[sfLPTokenBalance].get<Issue>();
            // Adjust the reserve if LP doesn't have LPToken trustline
            auto const sle =
                ctx.view.read(keylet::trustLine(accountID, lpIssue.account, lpIssue.currency));
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

    auto const amount = ctx.tx[~sfAmount];
    auto const amount2 = ctx.tx[~sfAmount2];
    auto const ammAccountID = ammSle->getAccountID(sfAccount);

    if (ctx.view.rules().enabled(fixCleanup3_3_0))
    {
        // Unified deposit freeze check for both pool assets.
        // AMMDeposit is not allowed if either asset is frozen.
        auto checkAsset = [&](Asset const& asset) -> TER {
            if (auto const ter = requireAuth(ctx.view, asset, accountID, AuthType::WeakAuth))
            {
                JLOG(ctx.j.debug()) << "AMM Deposit: account is not authorized, " << asset;
                return ter;
            }
            if (auto const ter = checkDepositFreeze(ctx.view, accountID, ammAccountID, asset))
            {
                JLOG(ctx.j.debug())
                    << "AMM Deposit: frozen, " << to_string(accountID) << " " << to_string(asset);
                return ter;
            }
            return tesSUCCESS;
        };

        if (auto const ter = checkAsset(ctx.tx[sfAsset]))
            return ter;

        if (auto const ter = checkAsset(ctx.tx[sfAsset2]))
            return ter;
    }
    else if (ctx.view.rules().enabled(featureAMMClawback))
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

            if (auto const ter = checkFrozen(ctx.view, accountID, asset); !isTesSuccess(ter))
            {
                JLOG(ctx.j.debug()) << "AMM Deposit: account or currency is frozen or locked, "
                                    << to_string(accountID) << " " << to_string(asset);

                return ter;
            }

            return tesSUCCESS;
        };

        if (auto const ter = checkAsset(ctx.tx[sfAsset]))
            return ter;

        if (auto const ter = checkAsset(ctx.tx[sfAsset2]))
            return ter;
    }

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
            if (!ctx.view.rules().enabled(fixCleanup3_3_0))
            {
                // AMM account or currency frozen
                if (auto const ter = checkFrozen(ctx.view, ammAccountID, amount->asset());
                    !isTesSuccess(ter))
                {
                    JLOG(ctx.j.debug())
                        << "AMM Deposit: AMM account or currency is frozen or locked, "
                        << to_string(accountID);
                    return ter;
                }
                // Account frozen
                if (auto const ter = checkIndividualFrozen(ctx.view, accountID, amount->asset());
                    !isTesSuccess(ter))
                {
                    JLOG(ctx.j.debug())
                        << "AMM Deposit: account is frozen or locked, " << to_string(accountID)
                        << " " << to_string(amount->asset());
                    return ter;
                }
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
    if (!ctx.tx.isFlag(tfLPToken))
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
        lpTokens && lpTokens->asset() != lptAMMBalance.asset())
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: invalid LPTokens.";
        return temBAD_AMM_TOKENS;
    }

    // Check the reserve for LPToken trustline if not LP.
    // We checked above but need to check again if depositing IOU only.
    if (ammLPHolds(ctx.view, *ammSle, accountID, ctx.j) == beast::kZero)
    {
        STAmount const xrpBalance = xrpLiquid(ctx.view, accountID, 1, ctx.j);
        // Insufficient reserve
        if (xrpBalance <= beast::kZero)
        {
            JLOG(ctx.j.debug()) << "AMM Instance: insufficient reserves";
            return tecINSUF_RESERVE_LINE;
        }
    }

    if (auto const ter = canMPTTradeAndTransfer(ctx.view, ctx.tx[sfAsset], accountID, accountID);
        !isTesSuccess(ter))
        return ter;
    if (auto const ter = canMPTTradeAndTransfer(ctx.view, ctx.tx[sfAsset2], accountID, accountID);
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
    auto const curveType = ctx_.tx.isFieldPresent(sfCurveType) ? ctx_.tx.getFieldU8(sfCurveType)
                                                               : std::uint8_t(CtConstantProduct);
    auto ammSle = sb.peek(keylet::amm(ctx_.tx[sfAsset], ctx_.tx[sfAsset2], curveType));
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
    auto const tfee = (lptAMMBalance == beast::kZero)
        ? ctx_.tx[~sfTradingFee].value_or(0)
        : getTradingFee(ctx_.view(), *ammSle, accountID_);

    // Concentrated Liquidity deposits create positions
    if (curveType == CtConcentratedLiquidity)
    {
        auto const tickLower = ctx_.tx[~sfTickLower];
        auto const tickUpper = ctx_.tx[~sfTickUpper];
        auto const subTxTypeCL = ctx_.tx.getFlags() & tfDepositSubTx;
        auto const isSingleAsset = (subTxTypeCL & tfSingleAsset) != 0u;

        if (!tickLower || !tickUpper)
            return {temMALFORMED, false};
        if (*tickLower >= *tickUpper)
            return {temMALFORMED, false};

        if (isSingleAsset)
        {
            if (!amount)
                return {temMALFORMED, false};
        }
        else
        {
            if (!amount || !amount2)
                return {temMALFORMED, false};
        }

        if (!ammSle->isFieldPresent(sfTickSpacing))
            return {tecINTERNAL, false};

        auto const tickSpacing = static_cast<std::int32_t>(ammSle->getFieldU16(sfTickSpacing));
        if (!isValidTick(*tickLower, tickSpacing) || !isValidTick(*tickUpper, tickSpacing))
            return {temMALFORMED, false};

        auto const currentTick = ammSle->getFieldI32(sfCurrentTick);
        auto const sqrtPriceCurrent = tickToSqrtPrice(currentTick);
        auto const sqrtPriceLower = tickToSqrtPrice(*tickLower);
        auto const sqrtPriceUpper = tickToSqrtPrice(*tickUpper);

        // Determine which pool asset corresponds to Amount/Amount2
        auto const asset1 = ctx_.tx[sfAsset];
        auto const asset2 = ctx_.tx[sfAsset2];

        Number liquidity;
        STAmount depositAmt0;
        STAmount depositAmt1;

        if (currentTick < *tickLower)
        {
            // Only token0 needed
            if (isSingleAsset && amount->asset() != asset1)
                return {tecAMM_FAILED, false};
            Number const amt0{*amount};
            liquidity = amt0 * sqrtPriceLower * sqrtPriceUpper / (sqrtPriceUpper - sqrtPriceLower);
            // For out-of-range, back-computation equals user amount
            depositAmt0 = *amount;
            depositAmt1 = STAmount{asset2, 0};
        }
        else if (currentTick >= *tickUpper)
        {
            // Only token1 needed
            if (isSingleAsset)
            {
                if (amount->asset() != asset2)
                    return {tecAMM_FAILED, false};
                Number const amt1{*amount};
                liquidity = amt1 / (sqrtPriceUpper - sqrtPriceLower);
                depositAmt1 = *amount;
            }
            else
            {
                Number const amt1{*amount2};
                liquidity = amt1 / (sqrtPriceUpper - sqrtPriceLower);
                depositAmt1 = *amount2;
            }
            depositAmt0 = STAmount{asset1, 0};
        }
        else
        {
            // Both tokens needed — single asset not allowed in-range
            if (isSingleAsset)
                return {tecAMM_FAILED, false};

            Number const amt0{*amount};
            Number const amt1{*amount2};
            auto const l0 =
                amt0 * sqrtPriceCurrent * sqrtPriceUpper / (sqrtPriceUpper - sqrtPriceCurrent);
            auto const l1 = amt1 / (sqrtPriceCurrent - sqrtPriceLower);
            liquidity = std::min(l0, l1);
            // The binding constraint's amount is fully used; the other
            // is scaled by the ratio of liquidity values
            if (l0 <= l1)
            {
                depositAmt0 = *amount;
                auto const frac = l0 / l1;
                depositAmt1 = getRoundedAsset(sb.rules(), *amount2, frac, IsDeposit::Yes);
            }
            else
            {
                depositAmt1 = *amount2;
                auto const frac = l1 / l0;
                depositAmt0 = getRoundedAsset(sb.rules(), *amount, frac, IsDeposit::Yes);
            }
        }

        if (liquidity <= Number{0})
            return {tecAMM_FAILED, false};

        auto const int64Max = Number(std::numeric_limits<std::int64_t>::max());
        if (liquidity > int64Max)
            return {tecAMM_FAILED, false};

        auto const liqU64 = static_cast<std::uint64_t>(static_cast<std::int64_t>(liquidity));

        // Transfer only the actual computed amounts, not user maximums
        if (depositAmt0 > beast::kZero)
        {
            if (auto const ter =
                    accountSend(sb, accountID_, ammAccountID, depositAmt0, ctx_.journal);
                !isTesSuccess(ter))
                return {ter, false};
        }
        if (depositAmt1 > beast::kZero)
        {
            if (auto const ter =
                    accountSend(sb, accountID_, ammAccountID, depositAmt1, ctx_.journal);
                !isTesSuccess(ter))
                return {ter, false};
        }

        auto const posKeylet =
            keylet::ammPosition(ammSle->key(), accountID_, ctx_.tx.getSeqProxy().value());
        auto posSle = std::make_shared<SLE>(posKeylet);
        // Read the pool's current fee-growth globals so the new
        // position/tick snapshots are seeded correctly. Without this,
        // a new position would claim all historical fees accumulated
        // in its range (per the v3 fee-growth-inside formula).
        auto const fgg0 = Number{ammSle->getFieldNumber(sfFeeGrowthGlobal0)};
        auto const fgg1 = Number{ammSle->getFieldNumber(sfFeeGrowthGlobal1)};

        (*posSle)[sfAccount] = accountID_;
        (*posSle)[sfAMMID] = ammSle->key();
        posSle->setFieldI32(sfTickLower, *tickLower);
        posSle->setFieldI32(sfTickUpper, *tickUpper);
        posSle->setFieldU64(sfPositionLiquidity, liqU64);
        // Initial fee-growth-inside snapshot must reflect the current
        // value at deposit time. We compute feeGrowthInside the same
        // way AMMCollectFees does (using the per-side tick outsides
        // we're about to write), but at deposit time the position
        // spans no swap history yet, so feeGrowthInside == 0 for any
        // valid (lower, upper, current) configuration if we set the
        // outsides per v3 convention below. Hence both lasts start at 0.
        posSle->setFieldNumber(sfFeeGrowthInsideLast0, STNumber{sfFeeGrowthInsideLast0, Number{0}});
        posSle->setFieldNumber(sfFeeGrowthInsideLast1, STNumber{sfFeeGrowthInsideLast1, Number{0}});
        posSle->setFieldAmount(sfTokensOwed0, STAmount{asset1, 0});
        posSle->setFieldAmount(sfTokensOwed1, STAmount{asset2, 0});
        sb.insert(posSle);

        auto const page =
            sb.dirInsert(keylet::ownerDir(accountID_), posKeylet, describeOwnerDir(accountID_));
        if (!page)
            return {tecDIR_FULL, false};
        (*posSle)[sfOwnerNode] = *page;
        sb.update(posSle);

        // Create or update tick entries for the position boundaries.
        // Per Uniswap v3, when a tick is first initialised its
        // feeGrowthOutside snapshot is taken under the convention that
        // "all prior fee growth happened on the side currentTick is on
        // now". So: if tick <= currentTick, all prior growth is below
        // → feeGrowthOutside = feeGrowthGlobal. Else 0.
        for (auto const tick : {*tickLower, *tickUpper})
        {
            auto const tickKeylet = keylet::ammTick(ammSle->key(), tick);
            auto tickSle = sb.peek(tickKeylet);
            bool const newlyInitialised = !tickSle;
            if (newlyInitialised)
            {
                tickSle = std::make_shared<SLE>(tickKeylet);
                (*tickSle)[sfAMMID] = ammSle->key();
                tickSle->setFieldI32(sfTickIndex, tick);
                tickSle->setFieldU64(sfLiquidityNet, 0);
                tickSle->setFieldU64(sfLiquidityGross, 0);
                bool const belowCurrent = tick <= currentTick;
                tickSle->setFieldNumber(
                    sfFeeGrowthOutside0,
                    STNumber{sfFeeGrowthOutside0, belowCurrent ? fgg0 : Number{0}});
                tickSle->setFieldNumber(
                    sfFeeGrowthOutside1,
                    STNumber{sfFeeGrowthOutside1, belowCurrent ? fgg1 : Number{0}});
                tickSle->setFieldU64(sfOwnerNode, 0);
                sb.insert(tickSle);
                // Mirror the initialise into the tick bitmap so the
                // bit-scan path in findNextTick can locate this tick
                // without a SHAMap pred/succ descent.
                if (auto const ter = setTickBitmap(sb, ammSle->key(), tick, ctx_.journal);
                    !isTesSuccess(ter))
                    return {ter, false};
            }
            auto gross = tickSle->getFieldU64(sfLiquidityGross);
            gross += liqU64;
            tickSle->setFieldU64(sfLiquidityGross, gross);
            // liquidityNet: +liq at lower tick, -liq at upper tick
            auto net = static_cast<std::int64_t>(tickSle->getFieldU64(sfLiquidityNet));
            net += (tick == *tickLower) ? static_cast<std::int64_t>(liqU64)
                                        : -static_cast<std::int64_t>(liqU64);
            tickSle->setFieldU64(sfLiquidityNet, static_cast<std::uint64_t>(net));
            sb.update(tickSle);
        }

        if (currentTick >= *tickLower && currentTick < *tickUpper)
        {
            auto activeLiq = ammSle->getFieldU64(sfActiveLiquidity);
            activeLiq += liqU64;
            ammSle->setFieldU64(sfActiveLiquidity, activeLiq);
        }

        // Track outstanding positions on the AMM SLE so AMMDelete can
        // reject removal while obligations remain (tecHAS_OBLIGATIONS).
        ammSle->setFieldU32(sfPositionCount, ammSle->getFieldU32(sfPositionCount) + 1);

        increaseOwnerCount(sb, accountID_, std::nullopt, 1, ctx_.journal);
        sb.update(ammSle);
        return {tesSUCCESS, true};
    }

    // Binned deposits add reserves to a single bin SLE; per-LP claim is
    // tracked via a ltAMM_BIN_HOLDING record (Phase 5 will migrate to
    // MPT shares for native composability).
    if (curveType == CtBinned)
    {
        auto const binIDOpt = ctx_.tx[~sfBinID];
        if (!binIDOpt)
            return {temMALFORMED, false};
        auto const binID = *binIDOpt;
        if (binID < minBinID || binID > maxBinID)
            return {temMALFORMED, false};
        if (!amount || !amount2)
            return {temMALFORMED, false};

        // Canonical asset ordering — sfAsset on the AMM SLE is the
        // lex-smaller asset (asset0). The tx fields may be in either
        // order; bin reserves are always stored as (asset0, asset1) in
        // canonical order regardless of tx ordering.
        auto const ammAsset0 = (*ammSle)[sfAsset];
        bool const txInOrder = (amount->asset() == ammAsset0);
        auto const deposit0 = txInOrder ? *amount : *amount2;
        auto const deposit1 = txInOrder ? *amount2 : *amount;

        // The bin must already exist — AMMBinCreate is responsible for
        // provisioning bins (and their MPT issuances). This separates
        // the MPT-create privilege from the deposit path so the latter
        // stays under MayAuthorizeMpt.
        auto const binKeylet = keylet::ammBin(ammSle->key(), binID);
        auto binSle = sb.peek(binKeylet);
        if (!binSle)
        {
            JLOG(j_.error()) << "AMM Deposit: bin " << binID
                             << " not provisioned. Submit AMMBinCreate first.";
            return {tecNO_ENTRY, false};
        }

        // Compute share allocation. First deposit seeds the bin and gets
        // baseline shares == amount's numeric drops value. Subsequent
        // deposits get proportional to existing outstanding shares.
        auto const reserve0Before = binSle->getFieldAmount(sfReserve0);
        auto const reserve1Before = binSle->getFieldAmount(sfReserve1);
        auto const outstandingBefore = binSle->getFieldU64(sfOutstandingAmount);

        std::uint64_t newShares = 0;
        if (outstandingBefore == 0)
        {
            // First deposit: shares = sqrt(amount0 * amount1) (CP-style
            // initial seeding, avoids gaming via lopsided deposits).
            Number const product = Number{deposit0} * Number{deposit1};
            if (product <= Number{0})
                return {tecAMM_FAILED, false};
            auto const seed = static_cast<std::int64_t>(root2(product));
            if (seed <= 0)
                return {tecAMM_FAILED, false};
            newShares = static_cast<std::uint64_t>(seed);
        }
        else
        {
            // Proportional: newShares = (deposit0 / reserve0) * outstanding.
            // Must match the asset1 side too: depositor must contribute
            // both sides in the bin's current ratio or be rounded down.
            auto const r0 = Number{reserve0Before};
            auto const r1 = Number{reserve1Before};
            if (r0 == Number{0} || r1 == Number{0})
                return {tecINTERNAL, false};
            auto const frac0 = Number{deposit0} / r0;
            auto const frac1 = Number{deposit1} / r1;
            // Use the smaller of the two fractions — proportional deposit
            // is capped by the less-supplied side. This stops a depositor
            // from claiming shares against the larger side alone.
            auto const frac = std::min(frac0, frac1);
            auto const proportional = frac * Number{static_cast<std::int64_t>(outstandingBefore)};
            if (proportional <= Number{0})
                return {tecAMM_FAILED, false};
            newShares = static_cast<std::uint64_t>(static_cast<std::int64_t>(proportional));
        }

        // Transfer assets from LP to AMM (use canonical-order amounts).
        if (auto const ter = accountSend(sb, accountID_, ammAccountID, deposit0, ctx_.journal);
            !isTesSuccess(ter))
            return {ter, false};
        if (auto const ter = accountSend(sb, accountID_, ammAccountID, deposit1, ctx_.journal);
            !isTesSuccess(ter))
            return {ter, false};

        // Mint the bin's MPT shares to the LP. AMMBinCreate provisioned
        // the issuance; here we ensure the LP holds it (authorize via
        // the reserve-exempt helper) then increment their balance +
        // the issuance's OutstandingAmount in lock-step.
        auto const mptIssuanceID = binSle->getFieldH192(sfMPTokenIssuanceID);
        if (!sb.exists(keylet::mptoken(mptIssuanceID, accountID_)))
        {
            if (auto const err = authorizeAMMIssuedMPT(
                    ApplyViewContext{sb, ctx_.tx},
                    preFeeBalance_,
                    mptIssuanceID,
                    accountID_,
                    ctx_.journal);
                !isTesSuccess(err))
                return {err, false};
        }
        {
            auto mptokenSle = sb.peek(keylet::mptoken(mptIssuanceID, accountID_));
            auto mptIssuanceSle = sb.peek(keylet::mptokenIssuance(mptIssuanceID));
            if (!mptokenSle || !mptIssuanceSle)
                return {tecINTERNAL, false};
            auto const prevHolder = mptokenSle->getFieldU64(sfMPTAmount);
            (*mptokenSle)[sfMPTAmount] = prevHolder + newShares;
            sb.update(mptokenSle);
            auto const prevOut = mptIssuanceSle->getFieldU64(sfOutstandingAmount);
            (*mptIssuanceSle)[sfOutstandingAmount] = prevOut + newShares;
            sb.update(mptIssuanceSle);
        }

        // Update bin reserves and outstanding shares.
        binSle->setFieldAmount(sfReserve0, reserve0Before + deposit0);
        binSle->setFieldAmount(sfReserve1, reserve1Before + deposit1);
        binSle->setFieldU64(sfOutstandingAmount, outstandingBefore + newShares);
        sb.update(binSle);

        // Find or create LP's snapshot record. The MPT balance (just
        // minted above) is the authoritative share quantity; this SLE
        // only stores the feeGrowth snapshot used by AMMCollectFees.
        // The reserve-exemption rule applies here too: holding-SLE
        // creation increments owner count, so we adjust -1 to keep
        // bin participation reserve-free.
        auto const holdingKeylet = keylet::ammBinHolding(ammSle->key(), accountID_, binID);
        auto holdingSle = sb.peek(holdingKeylet);
        Number const fg0Now = Number{binSle->getFieldNumber(sfFeeGrowthBin0)};
        Number const fg1Now = Number{binSle->getFieldNumber(sfFeeGrowthBin1)};

        if (!holdingSle)
        {
            holdingSle = std::make_shared<SLE>(holdingKeylet);
            (*holdingSle)[sfAccount] = accountID_;
            (*holdingSle)[sfAMMID] = ammSle->key();
            holdingSle->setFieldI32(sfBinID, binID);
            holdingSle->setFieldNumber(
                sfFeeGrowthInsideLast0, STNumber{sfFeeGrowthInsideLast0, fg0Now});
            holdingSle->setFieldNumber(
                sfFeeGrowthInsideLast1, STNumber{sfFeeGrowthInsideLast1, fg1Now});
            sb.insert(holdingSle);
            auto const page = sb.dirInsert(
                keylet::ownerDir(accountID_), holdingKeylet, describeOwnerDir(accountID_));
            if (!page)
                return {tecDIR_FULL, false};
            (*holdingSle)[sfOwnerNode] = *page;
            increaseOwnerCount(sb, accountID_, std::nullopt, 1, ctx_.journal);
            // Snapshot SLE is reserve-exempt — same rationale as the
            // AMM-issued MPT it tracks (both exist purely to support
            // AMM accounting). Helper compensates the owner-count++.
            exemptAMMOwnedSLE(sb, accountID_, ctx_.journal);
        }
        else
        {
            // Re-deposit: advance snapshot. Any fees accrued before
            // this deposit are forfeited — LP should collect first.
            holdingSle->setFieldNumber(
                sfFeeGrowthInsideLast0, STNumber{sfFeeGrowthInsideLast0, fg0Now});
            holdingSle->setFieldNumber(
                sfFeeGrowthInsideLast1, STNumber{sfFeeGrowthInsideLast1, fg1Now});
        }
        sb.update(holdingSle);

        // If this is the first time the bin gets shares (outstanding
        // was zero) AND the AMM's current activeBinID points at an
        // empty/non-existent bin, move activeBinID here so the
        // invariant's "active bin has liquidity" check passes.
        if (outstandingBefore == 0)
        {
            auto const activeNow = ammSle->getFieldI32(sfActiveBinID);
            auto const activeBinSle = sb.read(keylet::ammBin(ammSle->key(), activeNow));
            bool const activeIsEmpty =
                !activeBinSle || activeBinSle->getFieldU64(sfOutstandingAmount) == 0;
            if (activeIsEmpty)
                ammSle->setFieldI32(sfActiveBinID, binID);
        }
        sb.update(ammSle);
        return {tesSUCCESS, true};
    }

    auto const subTxType = ctx_.tx.getFlags() & tfDepositSubTx;

    auto dispatchToDeposit = [&,
                              &amountBalance = amountBalance,
                              &amount2Balance = amount2Balance,
                              &lptAMMBalance = lptAMMBalance]() -> std::pair<TER, STAmount> {
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
    };

    auto const [result, newLPTokenBalance] = [&]() -> std::pair<TER, STAmount> {
        try
        {
            return dispatchToDeposit();
        }
        catch (std::runtime_error const& e)
        {
            REACHABLE("xrpl::AMMDeposit::applyGuts : deposit amount out of range reached");
            // A deposit whose solved amount exceeds the integral asset's range
            // throws while converting to STAmount: past int64max
            // Number::operator rep() throws std::overflow_error; above the asset
            // maximum STAmount::canonicalize throws std::runtime_error. Fail
            // cleanly with a tec rather than letting it escape doApply as
            // tefEXCEPTION. Any other exception is left to propagate.
            // Gated by fixCleanup3_4_0 to preserve the legacy result pre-amendment.
            if (!sb.rules().enabled(fixCleanup3_4_0))
                throw;  // LCOV_EXCL_LINE - preserve legacy tefEXCEPTION
            JLOG(j_.error()) << "AMMDeposit: deposit amount out of range " << e.what();
            return std::make_pair(tecAMM_FAILED, STAmount{});
        }
    }();

    if (isTesSuccess(result))
    {
        XRPL_ASSERT(
            newLPTokenBalance > beast::kZero,
            "xrpl::AMMDeposit::applyGuts : valid new LP token balance");
        // Defensive check: deposit formulas with fixAMMv1_3 round LP tokens
        // down and asset amounts up, so sqrt(pool1*pool2) >= newLPTokenBalance
        // is guaranteed to hold. A precision loss failure is not expected.
        if (sb.rules().enabled(fixCleanup3_3_0) && sb.rules().enabled(fixAMMv1_3))
        {
            if (auto const ter = checkAMMPrecisionLoss(
                    sb, ammAccountID, ctx_.tx[sfAsset], ctx_.tx[sfAsset2], newLPTokenBalance, j_);
                !isTesSuccess(ter))
            {
                UNREACHABLE("xrpl::AMMDeposit::applyGuts : AMM precision loss");
                return {ter, false};  // LCOV_EXCL_LINE
            }
        }
        ammSle->setFieldAmount(sfLPTokenBalance, newLPTokenBalance);
        // LP depositing into AMM empty state gets the auction slot
        // and the voting
        if (lptAMMBalance == beast::kZero)
            initializeFeeAuctionVote(sb, ammSle, accountID_, lptAMMBalance.asset(), tfee);

        sb.update(ammSle);
    }

    return {result, isTesSuccess(result)};
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
        if (depositAmount <= beast::kZero)
            return temBAD_AMOUNT;
        if (isXRP(depositAmount))
        {
            auto const& lpIssue = lpTokensDeposit.get<Issue>();
            // Adjust the reserve if LP doesn't have LPToken trustline
            auto const sle =
                view.read(keylet::trustLine(accountID_, lpIssue.account, lpIssue.currency));
            if (xrpLiquid(view, accountID_, !sle, j_) >= depositAmount)
                return tesSUCCESS;
        }
        else if (
            accountFunds(
                view,
                accountID_,
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

    if (lpTokensDepositActual <= beast::kZero)
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
        accountID_,
        ammAccount,
        amountDepositActual,
        ctx_.journal,
        {},  // don't sponsor for AMM Trustline
        WaiveTransferFee::Yes);
    if (!isTesSuccess(res))
    {
        JLOG(ctx_.journal.debug()) << "AMM Deposit: failed to deposit " << amountDepositActual;
        return {res, STAmount{}};
    }

    // Deposit amount2Deposit
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
            view,
            accountID_,
            ammAccount,
            *amount2DepositActual,
            ctx_.journal,
            {},  // don't sponsor for AMM Trustline
            WaiveTransferFee::Yes);
        if (!isTesSuccess(res))
        {
            JLOG(ctx_.journal.debug())
                << "AMM Deposit: failed to deposit " << *amount2DepositActual;
            return {res, STAmount{}};
        }
    }

    // Deposit LP tokens
    res = accountSend(view, ammAccount, accountID_, lpTokensDepositActual, ctx_.journal);
    if (!isTesSuccess(res))
    {
        JLOG(ctx_.journal.debug()) << "AMM Deposit: failed to deposit LPTokens";
        return {res, STAmount{}};
    }

    return {tesSUCCESS, lptAMMBalance + lpTokensDepositActual};
}

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

/**
 * Proportional deposit of pools assets in exchange for the specified
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
        auto const tokensAdj = adjustLPTokensOut(view.rules(), lptAMMBalance, lpTokensDeposit);
        if (view.rules().enabled(fixAMMv1_3) && tokensAdj == beast::kZero)
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

/**
 * Proportional deposit of pool assets with the constraints on the maximum
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
    auto tokensAdj = getRoundedLPTokens(view.rules(), lptAMMBalance, frac, IsDeposit::Yes);
    if (tokensAdj == beast::kZero)
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
    if (tokensAdj == beast::kZero)
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

/**
 * Single asset deposit of the amount of asset specified by Asset1In.
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
    auto const tokens = adjustLPTokensOut(
        view.rules(), lptAMMBalance, lpTokensOut(amountBalance, amount, lptAMMBalance, tfee));
    if (tokens == beast::kZero)
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
    if (view.rules().enabled(fixAMMv1_3) && tokensAdj == beast::kZero)
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

/**
 * Single asset asset1 is deposited to obtain some share of
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
    auto const tokensAdj = adjustLPTokensOut(view.rules(), lptAMMBalance, lpTokensDeposit);
    if (view.rules().enabled(fixAMMv1_3) && tokensAdj == beast::kZero)
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

/**
 * Single asset deposit with two constraints.
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
    if (amount != beast::kZero)
    {
        auto const tokens = adjustLPTokensOut(
            view.rules(), lptAMMBalance, lpTokensOut(amountBalance, amount, lptAMMBalance, tfee));
        if (tokens <= beast::kZero)
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
        if (view.rules().enabled(fixAMMv1_3) && tokensAdj == beast::kZero)
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
    if (amountDeposit <= beast::kZero)
        return {tecAMM_FAILED, STAmount{}};
    auto tokNoRoundCb = [&] { return amountDeposit / ePrice; };
    auto tokProdCb = [&] { return amountDeposit / ePrice; };
    auto const tokens =
        getRoundedLPTokens(view.rules(), tokNoRoundCb, lptAMMBalance, tokProdCb, IsDeposit::Yes);
    // factor in the adjusted tokens
    auto const [tokensAdj, amountDepositAdj] = adjustAssetInByTokens(
        view.rules(), amountBalance, amountDeposit, lptAMMBalance, tokens, tfee);
    if (view.rules().enabled(fixAMMv1_3) && tokensAdj == beast::kZero)
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
AMMDeposit::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
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
