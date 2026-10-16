#include <xrpl/tx/transactors/dex/AMMWithdraw.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/AMMCurve.h>
#include <xrpl/ledger/helpers/AMMHelpers.h>
#include <xrpl/ledger/helpers/AMMTickMath.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
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

#include <algorithm>
#include <bit>
#include <cstdint>
#include <exception>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace xrpl {

bool
AMMWithdraw::checkExtraFeatures(PreflightContext const& ctx)
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
AMMWithdraw::getFlagsMask(PreflightContext const& ctx)
{
    return tfAMMWithdrawMask;
}

NotTEC
AMMWithdraw::preflight(PreflightContext const& ctx)
{
    auto const flags = ctx.tx.getFlags();

    auto const amount = ctx.tx[~sfAmount];
    auto const amount2 = ctx.tx[~sfAmount2];
    auto const ePrice = ctx.tx[~sfEPrice];
    auto const lpTokens = ctx.tx[~sfLPTokenIn];
    // Valid combinations are:
    //   LPTokens
    //   tfWithdrawAll
    //   Amount
    //   tfOneAssetWithdrawAll & Amount
    //   Amount and Amount2
    //   Amount and LPTokens
    //   Amount and EPrice
    // Binned partial-withdraw uses sfShares with no sub-tx flag —
    // exempt this case from the popcount==1 check that other curves use.
    auto const earlyCurveType = ctx.tx[~sfCurveType].value_or(std::uint8_t(CtConstantProduct));
    bool const isBinnedPartial = (earlyCurveType == CtBinned) && ctx.tx.isFieldPresent(sfShares) &&
        ((flags & tfWithdrawSubTx) == 0);
    if (!isBinnedPartial && std::popcount(flags & tfWithdrawSubTx) != 1)
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid flags.";
        return temMALFORMED;
    }
    if (ctx.tx.isFlag(tfLPToken))
    {
        if (!lpTokens || amount || amount2 || ePrice)
            return temMALFORMED;
    }
    else if (ctx.tx.isFlag(tfWithdrawAll))
    {
        if (lpTokens || amount || amount2 || ePrice)
            return temMALFORMED;
    }
    else if (ctx.tx.isFlag(tfOneAssetWithdrawAll) || ctx.tx.isFlag(tfSingleAsset))
    {
        if (!amount || lpTokens || amount2 || ePrice)
            return temMALFORMED;
    }
    else if (ctx.tx.isFlag(tfTwoAsset))
    {
        if (!amount || !amount2 || lpTokens || ePrice)
            return temMALFORMED;
    }
    else if (ctx.tx.isFlag(tfOneAssetLPToken))
    {
        if (!amount || !lpTokens || amount2 || ePrice)
            return temMALFORMED;
    }
    else if (ctx.tx.isFlag(tfLimitLPToken))
    {
        if (!amount || !ePrice || lpTokens || amount2)
            return temMALFORMED;
    }

    auto const asset = ctx.tx[sfAsset];
    auto const asset2 = ctx.tx[sfAsset2];
    if (auto const res = invalidAMMAssetPair(asset, asset2))
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: Invalid asset pair.";
        return res;
    }

    if (amount && amount2 && amount->asset() == amount2->asset())
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid tokens, same issue." << amount->asset() << " "
                            << amount2->asset();
        return temBAD_AMM_TOKENS;
    }

    if (lpTokens && *lpTokens <= beast::kZero)
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid tokens.";
        return temBAD_AMM_TOKENS;
    }

    if (amount)
    {
        if (auto const res = invalidAMMAmount(
                *amount,
                std::make_optional(std::make_pair(asset, asset2)),
                ((flags & (tfOneAssetWithdrawAll | tfOneAssetLPToken)) != 0u) || ePrice))
        {
            JLOG(ctx.j.debug()) << "AMM Withdraw: invalid Asset1Out";
            return res;
        }
    }

    if (amount2)
    {
        if (auto const res =
                invalidAMMAmount(*amount2, std::make_optional(std::make_pair(asset, asset2))))
        {
            JLOG(ctx.j.debug()) << "AMM Withdraw: invalid Asset2OutAmount";
            return res;
        }
    }

    if (ePrice)
    {
        if (auto const res = invalidAMMAmount(*ePrice))
        {
            JLOG(ctx.j.debug()) << "AMM Withdraw: invalid EPrice";
            return res;
        }
    }

    auto const curveType = ctx.tx[~sfCurveType].value_or(std::uint8_t(CtConstantProduct));

    if (curveType == CtConcentratedLiquidity)
    {
        if (!ctx.rules.enabled(featureAMMCurves))
            return temDISABLED;

        // CL withdrawals only support tfWithdrawAll
        if ((flags & tfWithdrawSubTx) != tfWithdrawAll)
        {
            JLOG(ctx.j.debug()) << "AMM Withdraw: invalid flags for CL pool.";
            return temMALFORMED;
        }

        // Position ID is required for CL withdrawal
        if (!ctx.tx.isFieldPresent(sfPositionID))
        {
            JLOG(ctx.j.debug()) << "AMM Withdraw: position ID required for CL pool.";
            return temMALFORMED;
        }

        // CL withdrawal must not have amount/ePrice/lpTokens fields
        if (amount || amount2 || ePrice || lpTokens)
        {
            JLOG(ctx.j.debug()) << "AMM Withdraw: amount fields not allowed for CL.";
            return temMALFORMED;
        }
    }
    else if (curveType == CtBinned)
    {
        if (!ctx.rules.enabled(featureAMMCurves))
            return temDISABLED;

        // Binned withdrawals: tfWithdrawAll (burn all) OR sfShares
        // (partial burn — specifies how many shares to redeem).
        bool const isAll = (flags & tfWithdrawSubTx) == tfWithdrawAll;
        bool const hasShares = ctx.tx.isFieldPresent(sfShares);
        if (isAll == hasShares)
        {
            JLOG(ctx.j.debug())
                << "AMM Withdraw: binned needs exactly one of tfWithdrawAll or sfShares.";
            return temMALFORMED;
        }
        if (isAll && (flags & tfWithdrawSubTx) != tfWithdrawAll)
        {
            JLOG(ctx.j.debug()) << "AMM Withdraw: invalid flags for binned pool.";
            return temMALFORMED;
        }

        if (!ctx.tx.isFieldPresent(sfBinID))
        {
            JLOG(ctx.j.debug()) << "AMM Withdraw: BinID required for binned pool.";
            return temMALFORMED;
        }
        if (amount || amount2 || ePrice || lpTokens)
        {
            JLOG(ctx.j.debug()) << "AMM Withdraw: amount fields not allowed for binned.";
            return temMALFORMED;
        }
        if (hasShares && ctx.tx.getFieldU64(sfShares) == 0)
        {
            JLOG(ctx.j.debug()) << "AMM Withdraw: Shares must be non-zero.";
            return temMALFORMED;
        }
        auto const binID = ctx.tx.getFieldI32(sfBinID);
        if (binID < minBinID || binID > maxBinID)
        {
            JLOG(ctx.j.debug()) << "AMM Withdraw: BinID out of bounds.";
            return temMALFORMED;
        }
    }
    else
    {
        // Non-CL/non-Binned pools must not have position or bin fields
        if (ctx.tx.isFieldPresent(sfPositionID) || ctx.tx.isFieldPresent(sfPositionLiquidity) ||
            ctx.tx.isFieldPresent(sfBinID))
        {
            JLOG(ctx.j.debug()) << "AMM Withdraw: position/bin fields not allowed "
                                   "for non-CL/Binned pool.";
            return temMALFORMED;
        }
    }

    return tesSUCCESS;
}

static std::optional<STAmount>
tokensWithdraw(
    STAmount const& lpTokens,
    std::optional<STAmount> const& tokensIn,
    std::uint32_t flags)
{
    if ((flags & (tfWithdrawAll | tfOneAssetWithdrawAll)) != 0u)
        return lpTokens;
    return tokensIn;
}

TER
AMMWithdraw::preclaim(PreclaimContext const& ctx)
{
    auto const accountID = ctx.tx[sfAccount];

    auto const curveType = ctx.tx.isFieldPresent(sfCurveType) ? ctx.tx.getFieldU8(sfCurveType)
                                                              : std::uint8_t(CtConstantProduct);
    auto const ammSle = ctx.view.read(keylet::amm(ctx.tx[sfAsset], ctx.tx[sfAsset2], curveType));
    if (!ammSle)
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: Invalid asset pair.";
        return terNO_AMM;
    }

    auto const amount = ctx.tx[~sfAmount];
    auto const amount2 = ctx.tx[~sfAmount2];

    auto const expected = ammHolds(
        ctx.view,
        *ammSle,
        amount ? amount->asset() : std::optional<Asset>{},
        amount2 ? amount2->asset() : std::optional<Asset>{},
        FreezeHandling::IgnoreFreeze,
        AuthHandling::IgnoreAuth,
        ctx.j);
    if (!expected)
        return expected.error();
    auto const [amountBalance, amount2Balance, lptAMMBalance] = *expected;
    if (curveType != CtConcentratedLiquidity && curveType != CtBinned)
    {
        if (lptAMMBalance == beast::kZero)
            return tecAMM_EMPTY;
        if (amountBalance <= beast::kZero || amount2Balance <= beast::kZero ||
            lptAMMBalance < beast::kZero)
        {
            // LCOV_EXCL_START
            JLOG(ctx.j.debug()) << "AMM Withdraw: reserves or tokens balance is zero.";
            return tecINTERNAL;
            // LCOV_EXCL_STOP
        }
    }

    auto const ammAccountID = ammSle->getAccountID(sfAccount);

    auto checkAmount = [&](std::optional<STAmount> const& amount, auto const& balance) -> TER {
        if (amount)
        {
            if (amount > balance)
            {
                JLOG(ctx.j.debug())
                    << "AMM Withdraw: withdrawing more than the balance, " << *amount;
                return tecAMM_BALANCE;
            }
            // WeakAuth - MPToken is created if it doesn't exist.
            if (auto const ter =
                    requireAuth(ctx.view, amount->asset(), accountID, AuthType::WeakAuth))
            {
                JLOG(ctx.j.debug())
                    << "AMM Withdraw: account is not authorized, " << amount->asset();
                return ter;
            }
            if (ctx.view.rules().enabled(fixCleanup3_3_0))
            {
                if (auto const ret = checkWithdrawFreeze(
                        ctx.view, ammAccountID, accountID, accountID, amount->asset()))
                {
                    JLOG(ctx.j.debug()) << "AMM Withdraw: frozen, " << to_string(accountID) << " "
                                        << to_string(amount->asset());
                    return ret;
                }
            }
            else
            {
                // AMM account or currency frozen
                if (auto const ter = checkFrozen(ctx.view, ammAccountID, amount->asset());
                    !isTesSuccess(ter))
                {
                    JLOG(ctx.j.debug())
                        << "AMM Withdraw: AMM account or currency is frozen or locked, "
                        << to_string(accountID);
                    return ter;
                }
                // Account frozen
                if (auto const ter = checkIndividualFrozen(ctx.view, accountID, amount->asset());
                    !isTesSuccess(ter))
                {
                    JLOG(ctx.j.debug())
                        << "AMM Withdraw: account is frozen or locked, " << to_string(accountID)
                        << " " << to_string(amount->asset());
                    return ter;
                }
            }
        }
        return tesSUCCESS;
    };

    if (auto const ter = checkAmount(amount, amountBalance))
        return ter;

    if (auto const ter = checkAmount(amount2, amount2Balance))
        return ter;

    if (curveType == CtConcentratedLiquidity)
    {
        // Validate position exists and is owned by caller
        auto const positionID = ctx.tx[sfPositionID];
        auto const posSle = ctx.view.read(keylet::ammPosition(positionID));
        if (!posSle)
        {
            JLOG(ctx.j.debug()) << "AMM Withdraw: position not found.";
            return tecNO_ENTRY;
        }
        if ((*posSle)[sfAccount] != accountID)
        {
            JLOG(ctx.j.debug()) << "AMM Withdraw: not position owner.";
            return tecNO_PERMISSION;
        }
        // Validate partial withdrawal amount
        if (ctx.tx.isFieldPresent(sfPositionLiquidity))
        {
            auto const withdrawLiq = ctx.tx.getFieldU64(sfPositionLiquidity);
            auto const posLiq = posSle->getFieldU64(sfPositionLiquidity);
            if (withdrawLiq == 0 || withdrawLiq > posLiq)
            {
                JLOG(ctx.j.debug()) << "AMM Withdraw: invalid position liquidity.";
                return temMALFORMED;
            }
        }
    }
    else if (curveType == CtBinned)
    {
        // Validate the bin exists and the LP holds at least one MPT
        // share for it. MPT balance is authoritative; the snapshot SLE
        // is auto-created on demand by AMMCollectFees so its absence
        // here is not an error.
        auto const binID = ctx.tx.getFieldI32(sfBinID);
        auto const binSle = ctx.view.read(keylet::ammBin(ammSle->key(), binID));
        if (!binSle)
            return tecNO_ENTRY;
        auto const mptId = binSle->getFieldH192(sfMPTokenIssuanceID);
        auto const mptokenSle = ctx.view.read(keylet::mptoken(mptId, accountID));
        if (!mptokenSle)
            return tecNO_ENTRY;
        if (mptokenSle->getFieldU64(sfMPTAmount) == 0)
            return tecAMM_FAILED;
    }
    else
    {
        // LP token validation for non-CL/non-Binned pools
        auto const lpTokens = ammLPHolds(ctx.view, *ammSle, ctx.tx[sfAccount], ctx.j);
        auto const lpTokensWithdraw =
            tokensWithdraw(lpTokens, ctx.tx[~sfLPTokenIn], ctx.tx.getFlags());

        if (lpTokens <= beast::kZero)
        {
            JLOG(ctx.j.debug()) << "AMM Withdraw: tokens balance is zero.";
            return tecAMM_BALANCE;
        }

        if (lpTokensWithdraw && lpTokensWithdraw->asset() != lpTokens.asset())
        {
            JLOG(ctx.j.debug()) << "AMM Withdraw: invalid LPTokens.";
            return temBAD_AMM_TOKENS;
        }

        if (lpTokensWithdraw && *lpTokensWithdraw > lpTokens)
        {
            JLOG(ctx.j.debug()) << "AMM Withdraw: invalid tokens.";
            return tecAMM_INVALID_TOKENS;
        }

        if (auto const ePrice = ctx.tx[~sfEPrice]; ePrice && ePrice->asset() != lpTokens.asset())
        {
            JLOG(ctx.j.debug()) << "AMM Withdraw: invalid EPrice.";
            return temBAD_AMM_TOKENS;
        }

        if ((ctx.tx.getFlags() & (tfLPToken | tfWithdrawAll)) != 0u)
        {
            if (auto const ter = checkAmount(amountBalance, amountBalance))
                return ter;
            if (auto const ter = checkAmount(amount2Balance, amount2Balance))
                return ter;
        }
    }

    return tesSUCCESS;
}

FreezeHandling
AMMWithdraw::issuerFreezeHandling() const
{
    // When the withdrawer is the issuer of a pool asset, the issuer can
    // always receive their own token — even when the pool is frozen.
    // Use IgnoreFreeze so ammHolds returns real balances instead of zero.
    if (!ctx_.view().rules().enabled(fixCleanup3_3_0))
        return FreezeHandling::ZeroIfFrozen;

    auto const asset1 = Asset{ctx_.tx[sfAsset]};
    auto const asset2 = Asset{ctx_.tx[sfAsset2]};
    if (!asset1.native() && accountID_ == asset1.getIssuer())
        return FreezeHandling::IgnoreFreeze;
    if (!asset2.native() && accountID_ == asset2.getIssuer())
        return FreezeHandling::IgnoreFreeze;

    return FreezeHandling::ZeroIfFrozen;
}

std::pair<TER, bool>
AMMWithdraw::applyGuts(Sandbox& sb)
{
    auto const amount = ctx_.tx[~sfAmount];
    auto const amount2 = ctx_.tx[~sfAmount2];
    auto const ePrice = ctx_.tx[~sfEPrice];
    auto const curveType = ctx_.tx.isFieldPresent(sfCurveType) ? ctx_.tx.getFieldU8(sfCurveType)
                                                               : std::uint8_t(CtConstantProduct);
    auto ammSle = sb.peek(keylet::amm(ctx_.tx[sfAsset], ctx_.tx[sfAsset2], curveType));
    if (!ammSle)
        return {tecINTERNAL, false};  // LCOV_EXCL_LINE
    auto const ammAccountID = (*ammSle)[sfAccount];
    auto const accountSle = sb.read(keylet::account(ammAccountID));
    if (!accountSle)
        return {tecINTERNAL, false};  // LCOV_EXCL_LINE
    // CL and Binned pools don't use fungible LP tokens — skip LP token
    // operations and go straight to position-based / bin-based withdrawal
    if (curveType != CtConcentratedLiquidity && curveType != CtBinned)
    {
        // Due to rounding, the LPTokenBalance of the last LP
        // might not match the LP's trustline balance
        if (sb.rules().enabled(fixAMMv1_1))
        {
            auto const lpTokensCheck =
                ammLPHolds(ctx_.view(), *ammSle, ctx_.tx[sfAccount], ctx_.journal);
            if (auto const res =
                    verifyAndAdjustLPTokenBalance(sb, lpTokensCheck, ammSle, accountID_);
                !res)
                return {res.error(), false};
        }
    }

    auto const tfee = getTradingFee(ctx_.view(), *ammSle, accountID_);

    auto const freezeHandling = issuerFreezeHandling();

    auto const expected = ammHolds(
        sb,
        *ammSle,
        amount ? amount->asset() : std::optional<Asset>{},
        amount2 ? amount2->asset() : std::optional<Asset>{},
        freezeHandling,
        AuthHandling::ZeroIfUnauthorized,
        ctx_.journal);
    if (!expected)
        return {expected.error(), false};
    auto const [amountBalance, amount2Balance, lptAMMBalance] = *expected;

    // Concentrated Liquidity withdrawals operate on positions
    if (curveType == CtConcentratedLiquidity)
    {
        auto const positionID = ctx_.tx[sfPositionID];
        auto posSle = sb.peek(keylet::ammPosition(positionID));
        if (!posSle)
            return {tecNO_ENTRY, false};

        auto const currentTick = ammSle->getFieldI32(sfCurrentTick);
        auto const tickLower = posSle->getFieldI32(sfTickLower);
        auto const tickUpper = posSle->getFieldI32(sfTickUpper);
        auto const posLiquidity = posSle->getFieldU64(sfPositionLiquidity);

        // Determine withdrawal amount: partial or full
        auto const isPartial = ctx_.tx.isFieldPresent(sfPositionLiquidity);
        auto const withdrawLiq =
            isPartial ? ctx_.tx.getFieldU64(sfPositionLiquidity) : posLiquidity;

        if (withdrawLiq == 0)
            return {tecAMM_FAILED, false};

        auto const sqrtPriceCurrent = tickToSqrtPrice(currentTick);
        auto const sqrtPriceLower = tickToSqrtPrice(tickLower);
        auto const sqrtPriceUpper = tickToSqrtPrice(tickUpper);
        Number const liq{static_cast<std::int64_t>(withdrawLiq)};

        // Compute withdrawal amounts from position geometry
        auto const asset1 = ctx_.tx[sfAsset];
        auto const asset2 = ctx_.tx[sfAsset2];
        STAmount withdrawAmt0;
        STAmount withdrawAmt1;

        if (currentTick < tickLower)
        {
            // Position is entirely token0
            auto const frac =
                liq * (sqrtPriceUpper - sqrtPriceLower) / (sqrtPriceLower * sqrtPriceUpper);
            if (frac <= Number{0})
                return {tecAMM_FAILED, false};
            withdrawAmt0 = getRoundedAsset(
                sb.rules(), amountBalance, frac / Number{amountBalance}, IsDeposit::No);
            withdrawAmt1 = STAmount{asset2, 0};
        }
        else if (currentTick >= tickUpper)
        {
            // Position is entirely token1
            auto const amt1 = liq * (sqrtPriceUpper - sqrtPriceLower);
            if (amt1 <= Number{0})
                return {tecAMM_FAILED, false};
            withdrawAmt0 = STAmount{asset1, 0};
            withdrawAmt1 = getRoundedAsset(
                sb.rules(), amount2Balance, amt1 / Number{amount2Balance}, IsDeposit::No);
        }
        else
        {
            // Position spans current price — both tokens
            auto const amt0Num =
                liq * (sqrtPriceUpper - sqrtPriceCurrent) / (sqrtPriceCurrent * sqrtPriceUpper);
            auto const amt1Num = liq * (sqrtPriceCurrent - sqrtPriceLower);
            if (amt0Num <= Number{0} && amt1Num <= Number{0})
                return {tecAMM_FAILED, false};
            if (amt0Num > Number{0})
            {
                withdrawAmt0 = getRoundedAsset(
                    sb.rules(), amountBalance, amt0Num / Number{amountBalance}, IsDeposit::No);
            }
            else
            {
                withdrawAmt0 = STAmount{asset1, 0};
            }
            if (amt1Num > Number{0})
            {
                withdrawAmt1 = getRoundedAsset(
                    sb.rules(), amount2Balance, amt1Num / Number{amount2Balance}, IsDeposit::No);
            }
            else
            {
                withdrawAmt1 = STAmount{asset2, 0};
            }
        }

        // Transfer withdrawal amounts from AMM to user
        if (withdrawAmt0 > beast::kZero)
        {
            if (auto const ter = accountSend(
                    sb,
                    ammAccountID,
                    accountID_,
                    withdrawAmt0,
                    ctx_.journal,
                    {},
                    WaiveTransferFee::Yes);
                !isTesSuccess(ter))
                return {ter, false};
        }
        if (withdrawAmt1 > beast::kZero)
        {
            if (auto const ter = accountSend(
                    sb,
                    ammAccountID,
                    accountID_,
                    withdrawAmt1,
                    ctx_.journal,
                    {},
                    WaiveTransferFee::Yes);
                !isTesSuccess(ter))
                return {ter, false};
        }

        // Update tick entries
        for (auto const tick : {tickLower, tickUpper})
        {
            auto const tickKeylet = keylet::ammTick(ammSle->key(), tick);
            auto tickSle = sb.peek(tickKeylet);
            if (!tickSle)
                continue;

            auto gross = tickSle->getFieldU64(sfLiquidityGross);
            if (gross >= withdrawLiq)
            {
                gross -= withdrawLiq;
            }
            else
            {
                gross = 0;
            }
            tickSle->setFieldU64(sfLiquidityGross, gross);

            auto net = static_cast<std::int64_t>(tickSle->getFieldU64(sfLiquidityNet));
            net -= (tick == tickLower) ? static_cast<std::int64_t>(withdrawLiq)
                                       : -static_cast<std::int64_t>(withdrawLiq);
            tickSle->setFieldU64(sfLiquidityNet, static_cast<std::uint64_t>(net));

            if (gross == 0)
            {
                // No positions reference this tick — delete it and clear
                // its bit in the per-256-tick presence bitmap.
                sb.erase(tickSle);
                if (auto const ter = clearTickBitmap(sb, ammSle->key(), tick, ctx_.journal);
                    !isTesSuccess(ter))
                    return {ter, false};
            }
            else
            {
                sb.update(tickSle);
            }
        }

        // Update active liquidity if current tick is in position range
        if (currentTick >= tickLower && currentTick < tickUpper)
        {
            auto activeLiq = ammSle->getFieldU64(sfActiveLiquidity);
            if (activeLiq >= withdrawLiq)
            {
                activeLiq -= withdrawLiq;
            }
            else
            {
                activeLiq = 0;
            }
            ammSle->setFieldU64(sfActiveLiquidity, activeLiq);
        }

        if (isPartial)
        {
            // Partial withdrawal: reduce position liquidity
            posSle->setFieldU64(sfPositionLiquidity, posLiquidity - withdrawLiq);
            sb.update(posSle);
        }
        else
        {
            // Full withdrawal: delete position and clean up
            auto const ownerDirKeylet = keylet::ownerDir(accountID_);
            auto const ownerNode = posSle->getFieldU64(sfOwnerNode);
            if (!sb.dirRemove(ownerDirKeylet, ownerNode, posSle->key(), true))
            {
                JLOG(j_.error()) << "AMM Withdraw: failed to remove position "
                                    "from owner directory.";
                return {tecINTERNAL, false};
            }
            sb.erase(posSle);
            decreaseOwnerCount(sb, accountID_, std::nullopt, 1, ctx_.journal);

            // Decrement outstanding-position counter on the AMM SLE.
            auto const positions = ammSle->getFieldU32(sfPositionCount);
            ammSle->setFieldU32(sfPositionCount, positions > 0 ? positions - 1 : 0);
        }

        sb.update(ammSle);
        return {tesSUCCESS, true};
    }

    // Binned withdrawals: tfWithdrawAll burns the LP's full holding;
    // sfShares burns the specified amount and prorates reserve return.
    if (curveType == CtBinned)
    {
        auto const binIDOpt = ctx_.tx[~sfBinID];
        if (!binIDOpt)
        {
            JLOG(j_.error()) << "Binned withdraw: missing sfBinID";
            return {temMALFORMED, false};
        }
        auto const binID = *binIDOpt;

        auto const binKeylet = keylet::ammBin(ammSle->key(), binID);
        auto binSle = sb.peek(binKeylet);
        if (!binSle)
        {
            JLOG(j_.error()) << "Binned withdraw: bin SLE missing";
            return {tecNO_ENTRY, false};
        }

        // MPT balance is authoritative for share count. An LP who
        // received bin MPTs via transfer can redeem them here without
        // ever calling AMMDeposit — their snapshot SLE is created on
        // first AMMCollectFees with snapshot=now (transferred holders
        // forfeit past fees; collect before transferring to keep them).
        auto const mptIssuanceID = binSle->getFieldH192(sfMPTokenIssuanceID);
        auto mptokenSle = sb.peek(keylet::mptoken(mptIssuanceID, accountID_));
        if (!mptokenSle)
        {
            JLOG(j_.error()) << "Binned withdraw: LP holds no MPT for bin " << binID;
            return {tecNO_ENTRY, false};
        }
        auto const lpShares = mptokenSle->getFieldU64(sfMPTAmount);
        if (lpShares == 0)
        {
            JLOG(j_.error()) << "Binned withdraw: LP MPT balance zero";
            return {tecAMM_FAILED, false};
        }

        // Determine how many shares to burn.
        auto const isFullBurn = (ctx_.tx.getFlags() & tfWithdrawAll) == tfWithdrawAll;
        std::uint64_t sharesToBurn = lpShares;
        if (!isFullBurn)
        {
            sharesToBurn = ctx_.tx.getFieldU64(sfShares);
            if (sharesToBurn == 0 || sharesToBurn > lpShares)
                return {tecAMM_BALANCE, false};
        }

        // Snapshot SLE may or may not exist (auto-created by collect or
        // deposit; missing if LP received MPT via transfer with no
        // subsequent collect). Keep it around for future collects on
        // any remaining shares; if LP burns all, delete it.
        auto const holdingKeylet = keylet::ammBinHolding(ammSle->key(), accountID_, binID);
        auto holdingSle = sb.peek(holdingKeylet);

        auto const outstanding = binSle->getFieldU64(sfOutstandingAmount);
        if (outstanding == 0)
        {
            JLOG(j_.error()) << "Binned withdraw: outstanding zero";
            return {tecINTERNAL, false};
        }
        auto const reserve0 = binSle->getFieldAmount(sfReserve0);
        auto const reserve1 = binSle->getFieldAmount(sfReserve1);

        // Auto-collect accrued fees BEFORE burning shares so the LP
        // doesn't silently forfeit them. fee_owed = lpShares ×
        // (feeGrowth_now − snapshot). Computed against the full pre-burn
        // share balance, then snapshot advances to "now" before the burn.
        // If no snapshot SLE exists (rare — the LP received the MPT via
        // transfer and never collected or deposited), treat snapshot as
        // "now" and skip the auto-collect for this withdrawal — they
        // never had a claim on prior fees.
        Number const fg0Now = Number{binSle->getFieldNumber(sfFeeGrowthBin0)};
        Number const fg1Now = Number{binSle->getFieldNumber(sfFeeGrowthBin1)};
        if (holdingSle)
        {
            Number const fg0Last = Number{holdingSle->getFieldNumber(sfFeeGrowthInsideLast0)};
            Number const fg1Last = Number{holdingSle->getFieldNumber(sfFeeGrowthInsideLast1)};
            Number const sharesN{static_cast<std::int64_t>(lpShares)};
            Number const owed0 = sharesN * (fg0Now - fg0Last);
            Number const owed1 = sharesN * (fg1Now - fg1Last);
            // Cap each side at the bin's actual reserve before scaling
            // the redemption math (defensive — accumulator drift would
            // otherwise let withdraw overdraw the bin).
            auto const feeAmt0 = owed0 > Number{0} ? toSTAmount(reserve0.asset(), owed0)
                                                   : STAmount{reserve0.asset(), 0};
            auto const feeAmt1 = owed1 > Number{0} ? toSTAmount(reserve1.asset(), owed1)
                                                   : STAmount{reserve1.asset(), 0};
            if (feeAmt0 > beast::kZero && feeAmt0 <= reserve0)
            {
                if (auto const ter =
                        accountSend(sb, ammAccountID, accountID_, feeAmt0, ctx_.journal);
                    !isTesSuccess(ter))
                    return {ter, false};
                binSle->setFieldAmount(sfReserve0, reserve0 - feeAmt0);
            }
            if (feeAmt1 > beast::kZero && feeAmt1 <= reserve1)
            {
                if (auto const ter =
                        accountSend(sb, ammAccountID, accountID_, feeAmt1, ctx_.journal);
                    !isTesSuccess(ter))
                    return {ter, false};
                binSle->setFieldAmount(sfReserve1, reserve1 - feeAmt1);
            }
            // Advance snapshot to now — fees for any remaining shares
            // accrue from this point forward.
            holdingSle->setFieldNumber(
                sfFeeGrowthInsideLast0, STNumber{sfFeeGrowthInsideLast0, fg0Now});
            holdingSle->setFieldNumber(
                sfFeeGrowthInsideLast1, STNumber{sfFeeGrowthInsideLast1, fg1Now});
            sb.update(holdingSle);
        }

        // Re-read reserves — auto-collect above may have decremented
        // them. Proportional redemption against the burned shares is
        // computed against the post-fee reserves.
        auto const reserve0AfterFees = binSle->getFieldAmount(sfReserve0);
        auto const reserve1AfterFees = binSle->getFieldAmount(sfReserve1);
        auto const frac = Number{static_cast<std::int64_t>(sharesToBurn)} /
            Number{static_cast<std::int64_t>(outstanding)};
        auto const out0Number = Number{reserve0AfterFees} * frac;
        auto const out1Number = Number{reserve1AfterFees} * frac;
        auto const out0 = toSTAmount(reserve0AfterFees.asset(), out0Number);
        auto const out1 = toSTAmount(reserve1AfterFees.asset(), out1Number);

        // Send reserves back to LP.
        if (out0 > beast::kZero)
        {
            if (auto const ter = accountSend(sb, ammAccountID, accountID_, out0, ctx_.journal);
                !isTesSuccess(ter))
            {
                JLOG(j_.error()) << "Binned withdraw: accountSend out0 failed " << ter;
                return {ter, false};
            }
        }
        if (out1 > beast::kZero)
        {
            if (auto const ter = accountSend(sb, ammAccountID, accountID_, out1, ctx_.journal);
                !isTesSuccess(ter))
            {
                JLOG(j_.error()) << "Binned withdraw: accountSend out1 failed " << ter;
                return {ter, false};
            }
        }

        // Update bin reserves (against the post-auto-collect values).
        binSle->setFieldAmount(sfReserve0, reserve0AfterFees - out0);
        binSle->setFieldAmount(sfReserve1, reserve1AfterFees - out1);
        binSle->setFieldU64(sfOutstandingAmount, outstanding - sharesToBurn);

        // Burn the LP's MPT shares (decrement holder balance and the
        // issuance's outstanding amount). MPT is the authoritative
        // share record; the holding SLE only carries the per-LP
        // feeGrowth snapshot.
        {
            auto mptIssuanceSle = sb.peek(keylet::mptokenIssuance(mptIssuanceID));
            if (!mptIssuanceSle)
                return {tecINTERNAL, false};
            (*mptokenSle)[sfMPTAmount] = lpShares - sharesToBurn;
            sb.update(mptokenSle);
            auto const issOut = mptIssuanceSle->getFieldU64(sfOutstandingAmount);
            (*mptIssuanceSle)[sfOutstandingAmount] =
                issOut >= sharesToBurn ? issOut - sharesToBurn : 0;
            sb.update(mptIssuanceSle);
        }

        // Snapshot SLE: keep it as long as the LP retains any shares
        // (so future collects on the residual stake work). Delete on
        // full burn to free the directory slot. Owner-count is already
        // net-zero from the create-time exemption, so no adjustment on
        // delete either.
        auto const remainingShares = lpShares - sharesToBurn;
        if (remainingShares == 0 && holdingSle)
        {
            auto const ownerDirKeylet = keylet::ownerDir(accountID_);
            auto const ownerNode = holdingSle->getFieldU64(sfOwnerNode);
            if (!sb.dirRemove(ownerDirKeylet, ownerNode, holdingSle->key(), true))
            {
                JLOG(j_.error()) << "AMM Withdraw: failed to remove bin holding "
                                    "from owner directory.";
                return {tecINTERNAL, false};
            }
            sb.erase(holdingSle);
        }

        // Keep the bin SLE even on full drain — it owns the MPT
        // issuance reference that future re-deposits need. Empty bins
        // are still iterable but contribute zero liquidity.
        sb.update(binSle);

        // If this bin was the active one and is now empty, move the
        // activeBinID to the NEAREST non-empty bin in bin-ID distance
        // — preserves the "current price" semantic. Scanning in
        // owner-directory order would pick an arbitrary survivor.
        if (binSle->getFieldU64(sfOutstandingAmount) == 0)
        {
            auto const currentActive = ammSle->getFieldI32(sfActiveBinID);
            if (currentActive == binID)
            {
                std::optional<std::int32_t> nearest;
                std::int64_t nearestDistance = 0;
                forEachItem(sb, ammAccountID, [&](std::shared_ptr<SLE const> const& s) {
                    if (!s || s->getType() != ltAMM_BIN)
                        return;
                    if (!s->isFieldPresent(sfAMMID) || s->getFieldH256(sfAMMID) != ammSle->key())
                        return;
                    if (s->getFieldU64(sfOutstandingAmount) == 0)
                        return;
                    auto const candidate = s->getFieldI32(sfBinID);
                    auto const dist = std::abs(
                        static_cast<std::int64_t>(candidate) - static_cast<std::int64_t>(binID));
                    // On ties prefer the higher bin (price going up
                    // mid-trade is the conservative choice for a
                    // depleted-asset0 bin; symmetric on the other
                    // side. Pure tiebreaker; rare in practice).
                    if (!nearest || dist < nearestDistance ||
                        (dist == nearestDistance && candidate > *nearest))
                    {
                        nearest = candidate;
                        nearestDistance = dist;
                    }
                });
                if (nearest)
                    ammSle->setFieldI32(sfActiveBinID, *nearest);
            }
        }

        sb.update(ammSle);
        return {tesSUCCESS, true};
    }

    // Non-CL path: compute LP token state
    auto const lpTokens = ammLPHolds(ctx_.view(), *ammSle, ctx_.tx[sfAccount], ctx_.journal);
    auto const lpTokensWithdraw =
        tokensWithdraw(lpTokens, ctx_.tx[~sfLPTokenIn], ctx_.tx.getFlags());

    auto const subTxType = ctx_.tx.getFlags() & tfWithdrawSubTx;

    auto dispatchToWithdraw = [&,
                               &amountBalance = amountBalance,
                               &amount2Balance = amount2Balance,
                               &lptAMMBalance = lptAMMBalance]() -> std::pair<TER, STAmount> {
        if (subTxType & tfTwoAsset)
        {
            return equalWithdrawLimit(
                sb,
                *ammSle,
                ammAccountID,
                amountBalance,
                amount2Balance,
                lptAMMBalance,
                *amount,
                *amount2,
                tfee);
        }
        if (subTxType & tfOneAssetLPToken || subTxType & tfOneAssetWithdrawAll)
        {
            return singleWithdrawTokens(
                sb,
                *ammSle,
                ammAccountID,
                amountBalance,
                lptAMMBalance,
                *amount,
                *lpTokensWithdraw,
                tfee);
        }
        if (subTxType & tfLimitLPToken)
        {
            return singleWithdrawEPrice(
                sb, *ammSle, ammAccountID, amountBalance, lptAMMBalance, *amount, *ePrice, tfee);
        }
        if (subTxType & tfSingleAsset)
        {
            return singleWithdraw(
                sb, *ammSle, ammAccountID, amountBalance, lptAMMBalance, *amount, tfee);
        }
        if (subTxType & tfLPToken || subTxType & tfWithdrawAll)
        {
            return equalWithdrawTokens(
                sb,
                *ammSle,
                ammAccountID,
                amountBalance,
                amount2Balance,
                lptAMMBalance,
                lpTokens,
                *lpTokensWithdraw,
                tfee);
        }
        // should not happen.
        // LCOV_EXCL_START
        JLOG(j_.error()) << "AMM Withdraw: invalid options.";
        return std::make_pair(tecINTERNAL, STAmount{});
        // LCOV_EXCL_STOP
    };

    auto const [result, newLPTokenBalance] = [&]() -> std::pair<TER, STAmount> {
        try
        {
            return dispatchToWithdraw();
        }
        catch (std::runtime_error const& e)
        {
            // Defense in-depth for amount overflow/out-of-range: the withdrawal
            // counterpart of the AMMDeposit guard. Unlike deposit, no known
            // withdraw path can throw here - preclaim bounds the requested
            // amounts by the pool balances, and the only historical throw
            // (denom == 0 in singleWithdrawEPrice) is guarded under
            // fixCleanup3_3_0. Gated by fixCleanup3_4_0 to preserve the
            // legacy tefEXCEPTION pre-amendment.
            if (!sb.rules().enabled(fixCleanup3_4_0))
                throw;
            // LCOV_EXCL_START
            JLOG(j_.error()) << "AMMWithdraw: amount out of range " << e.what();
            return std::make_pair(tecAMM_FAILED, STAmount{});
            // LCOV_EXCL_STOP
        }
    }();

    if (!isTesSuccess(result))
        return {result, false};

    if (sb.rules().enabled(fixCleanup3_3_0) && sb.rules().enabled(fixAMMv1_3))
    {
        if (auto const ter = checkAMMPrecisionLoss(
                sb, ammAccountID, ctx_.tx[sfAsset], ctx_.tx[sfAsset2], newLPTokenBalance, j_);
            !isTesSuccess(ter))
        {
            return {ter, false};
        }
    }

    auto const res = deleteAMMAccountIfEmpty(
        sb, ammSle, newLPTokenBalance, ctx_.tx[sfAsset], ctx_.tx[sfAsset2], j_, curveType);
    // LCOV_EXCL_START
    if (!res.second)
        return {res.first, false};
    // LCOV_EXCL_STOP

    JLOG(ctx_.journal.trace()) << "AMM Withdraw: tokens " << to_string(newLPTokenBalance.iou())
                               << " " << to_string(lpTokens.iou()) << " "
                               << to_string(lptAMMBalance.iou());

    return {tesSUCCESS, true};
}

TER
AMMWithdraw::doApply()
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
AMMWithdraw::withdraw(
    Sandbox& view,
    SLE const& ammSle,
    AccountID const& ammAccount,
    STAmount const& amountBalance,
    STAmount const& amountWithdraw,
    std::optional<STAmount> const& amount2Withdraw,
    STAmount const& lpTokensAMMBalance,
    STAmount const& lpTokensWithdraw,
    std::uint16_t tfee)
{
    TER ter;
    STAmount newLPTokenBalance;
    std::tie(ter, newLPTokenBalance, std::ignore, std::ignore) = withdraw(
        view,
        ammSle,
        ammAccount,
        std::nullopt,
        accountID_,
        amountBalance,
        amountWithdraw,
        amount2Withdraw,
        lpTokensAMMBalance,
        lpTokensWithdraw,
        tfee,
        issuerFreezeHandling(),
        AuthHandling::ZeroIfUnauthorized,
        ReserveHandling::EnforceReserve,
        isWithdrawAll(ctx_.tx),
        preFeeBalance_,
        j_);
    return {ter, newLPTokenBalance};
}

std::tuple<TER, STAmount, STAmount, std::optional<STAmount>>
AMMWithdraw::withdraw(
    Sandbox& view,
    SLE const& ammSle,
    AccountID const& ammAccount,
    std::optional<AccountID> const& clawbackIssuer,
    AccountID const& account,
    STAmount const& amountBalance,
    STAmount const& amountWithdraw,
    std::optional<STAmount> const& amount2Withdraw,
    STAmount const& lpTokensAMMBalance,
    STAmount const& lpTokensWithdraw,
    std::uint16_t tfee,
    FreezeHandling freezeHandling,
    AuthHandling authHandling,
    ReserveHandling reserveHandling,
    WithdrawAll withdrawAll,
    XRPAmount const& priorBalance,
    beast::Journal const& journal)
{
    auto const lpTokens = ammLPHolds(view, ammSle, account, journal);
    auto const expected = ammHolds(
        view, ammSle, amountWithdraw.asset(), std::nullopt, freezeHandling, authHandling, journal);
    // LCOV_EXCL_START
    if (!expected)
        return {expected.error(), STAmount{}, STAmount{}, STAmount{}};
    // LCOV_EXCL_STOP
    auto const [curBalance, curBalance2, _] = *expected;
    (void)_;

    auto const [amountWithdrawActual, amount2WithdrawActual, lpTokensWithdrawActual] =
        [&]() -> std::tuple<STAmount, std::optional<STAmount>, STAmount> {
        if (withdrawAll == WithdrawAll::No)
        {
            return adjustAmountsByLPTokens(
                amountBalance,
                amountWithdraw,
                amount2Withdraw,
                lpTokensAMMBalance,
                lpTokensWithdraw,
                tfee,
                IsDeposit::No);
        }
        return std::make_tuple(amountWithdraw, amount2Withdraw, lpTokensWithdraw);
    }();

    if (lpTokensWithdrawActual <= beast::kZero || lpTokensWithdrawActual > lpTokens)
    {
        JLOG(journal.debug()) << "AMM Withdraw: failed to withdraw, invalid LP tokens: "
                              << lpTokensWithdrawActual << " " << lpTokens << " "
                              << lpTokensAMMBalance;
        return {tecAMM_INVALID_TOKENS, STAmount{}, STAmount{}, STAmount{}};
    }

    // Should not happen since the only LP on last withdraw
    // has the balance set to the lp token trustline balance.
    if (view.rules().enabled(fixAMMv1_1) && lpTokensWithdrawActual > lpTokensAMMBalance)
    {
        // LCOV_EXCL_START
        JLOG(journal.debug()) << "AMM Withdraw: failed to withdraw, unexpected LP tokens: "
                              << lpTokensWithdrawActual << " " << lpTokens << " "
                              << lpTokensAMMBalance;
        return {tecINTERNAL, STAmount{}, STAmount{}, STAmount{}};
        // LCOV_EXCL_STOP
    }

    // Withdrawing one side of the pool
    if ((amountWithdrawActual == curBalance && amount2WithdrawActual != curBalance2) ||
        (amount2WithdrawActual == curBalance2 && amountWithdrawActual != curBalance))
    {
        JLOG(journal.debug()) << "AMM Withdraw: failed to withdraw one side of the pool "
                              << " curBalance: " << curBalance << " " << amountWithdrawActual
                              << " lpTokensBalance: " << lpTokensWithdraw << " lptBalance "
                              << lpTokensAMMBalance;
        return {tecAMM_BALANCE, STAmount{}, STAmount{}, STAmount{}};
    }

    // May happen if withdrawing an amount close to one side of the pool
    if (lpTokensWithdrawActual == lpTokensAMMBalance &&
        (amountWithdrawActual != curBalance || amount2WithdrawActual != curBalance2))
    {
        JLOG(journal.debug()) << "AMM Withdraw: failed to withdraw all tokens "
                              << " curBalance: " << curBalance << " " << amountWithdrawActual
                              << " curBalance2: " << amount2WithdrawActual.value_or(STAmount{0})
                              << " lpTokensBalance: " << lpTokensWithdraw << " lptBalance "
                              << lpTokensAMMBalance;
        return {tecAMM_BALANCE, STAmount{}, STAmount{}, STAmount{}};
    }

    // Withdrawing more than the pool's balance
    if (amountWithdrawActual > curBalance || amount2WithdrawActual > curBalance2)
    {
        JLOG(journal.debug()) << "AMM Withdraw: withdrawing more than the pool's balance "
                              << " curBalance: " << curBalance << " " << amountWithdrawActual
                              << " curBalance2: " << curBalance2 << " "
                              << (amount2WithdrawActual ? *amount2WithdrawActual : STAmount{})
                              << " lpTokensBalance: " << lpTokensWithdraw << " lptBalance "
                              << lpTokensAMMBalance;
        return {tecAMM_BALANCE, STAmount{}, STAmount{}, STAmount{}};
    }

    // Updated pool state must be valid - either all balances are zero
    // or all balances are non-zero.
    if (view.rules().enabled(featureMPTokensV2))
    {
        bool const newBalanceZero = (curBalance - amountWithdrawActual) == beast::kZero;
        bool const newBalance2Zero =
            (curBalance2 - amount2WithdrawActual.value_or(curBalance2.asset())) == beast::kZero;
        bool const newLPTokensZero = (lpTokensAMMBalance - lpTokensWithdrawActual) == beast::kZero;
        // newBalance2Zero can be zero if that side of the pool is frozen.
        // ignore newBalance2Zero if one-sided withdrawal.
        bool const valid = [&]() {
            if (!amount2WithdrawActual)
                return newBalanceZero == newLPTokensZero;
            return newBalanceZero == newBalance2Zero && newBalance2Zero == newLPTokensZero;
        }();
        if (!valid)
        {
            JLOG(journal.debug()) << "AMM Withdraw: some balances are zero"
                                  << " curBalance: " << curBalance << " " << amountWithdrawActual
                                  << " curBalance2: " << curBalance2 << " "
                                  << (amount2WithdrawActual ? *amount2WithdrawActual : STAmount{})
                                  << " lpTokensBalance: " << lpTokensWithdraw << " lptBalance "
                                  << lpTokensAMMBalance;
            return {tecAMM_BALANCE, STAmount{}, STAmount{}, STAmount{}};
        }
    }

    // Check the reserve in case a trustline or MPT has to be created
    bool const enabledFixAmMv12 = view.rules().enabled(fixAMMv1_2);
    // If seated after a call to sufficientReserve() then MPToken must be
    // authorized
    std::optional<Keylet> mptokenKey;
    auto sufficientReserve = [&](Asset const& asset) -> TER {
        mptokenKey = std::nullopt;
        if (!enabledFixAmMv12 || isXRP(asset))
            return tesSUCCESS;
        bool const assetNotExists = asset.visit(
            [&](Issue const& issue) { return !view.exists(keylet::trustLine(account, issue)); },
            [&](MPTIssue const& issue) {
                auto const issuanceKey = keylet::mptokenIssuance(issue);
                mptokenKey = keylet::mptoken(issuanceKey.key, account);
                if (!view.exists(*mptokenKey))
                    return true;
                mptokenKey = std::nullopt;
                return false;
            });
        if (assetNotExists)
        {
            // Intentionally ignore the reserve check for AMMClawback, so the
            // holder can not avoid clawback by deleting the trustline/MPToken
            // and keeping a low spendable balance. AMMClawback has a higher
            // priority than the reserve check.
            if (view.rules().enabled(fixCleanup3_4_0) &&
                reserveHandling == ReserveHandling::IgnoreReserve)
                return tesSUCCESS;

            auto sleAccount = view.peek(keylet::account(account));
            if (!sleAccount)
                return tecINTERNAL;  // LCOV_EXCL_LINE

            auto const balance = (*sleAccount)[sfBalance]->xrp();
            // See also TrustSet::doApply() and MPTokenAuthorize::authorize()
            XRPAmount const reserve(
                (ownerCount(sleAccount, journal) < 2)
                    ? XRPAmount(beast::kZero)
                    : accountReserve(view, sleAccount, journal, {.ownerCountDelta = 1}));

            auto const balanceAdj = std::max(priorBalance, balance);
            if (balanceAdj < reserve)
                return tecINSUFFICIENT_RESERVE;
        }
        return tesSUCCESS;
    };

    // Create MPToken if it doesn't exist
    auto createMPToken = [&](Asset const& asset) -> TER {
        // If mptoken is seated then must authorize
        if (mptokenKey && account != asset.getIssuer())
        {
            auto const& mptIssue = asset.get<MPTIssue>();
            std::uint32_t createFlags = 0;
            if (auto const err = requireAuth(view, mptIssue, account, AuthType::WeakAuth);
                !isTesSuccess(err))
            {
                if (authHandling != AuthHandling::IgnoreAuth || err != tecNO_AUTH)
                {
                    // Unreachable in practice. Normal withdraws (authHandling
                    // != IgnoreAuth) are rejected for unauthorized holders in
                    // preclaim, so they never get here. Under clawback
                    // (IgnoreAuth) requireAuth returns a non-tecNO_AUTH error
                    // (e.g. tecEXPIRED) only for a domain-authorized MPT, but no
                    // such MPT can be in an AMM pool: a directly domain-gated
                    // RequireAuth MPT fails AMMCreate/deposit with tecNO_AUTH,
                    // and vault shares (whose recursive auth could yield
                    // tecEXPIRED) are rejected by AMMCreate with tecWRONG_ASSET.
                    return err;  // LCOV_EXCL_LINE
                }

                // AMMClawback ignores authorization so the issuer can recover
                // MPT locked in the pool even if the holder deleted their
                // MPToken. Only auto-authorize the recreated MPToken for the
                // clawback issuer's own asset: authorization is granted by an
                // asset's issuer, and the clawback transaction is signed by
                // that issuer only for its own asset. For a paired asset issued
                // by a different account, recreate the MPToken *unauthorized* so
                // the clawback does not grant authorization on behalf of that
                // issuer (which would bypass its lsfMPTRequireAuth). The holder
                // still receives the paired asset (accountSend only requires the
                // MPToken to exist, not to be authorized); the balance remains
                // gated by its issuer until that issuer authorizes it.
                if (clawbackIssuer && asset.getIssuer() == *clawbackIssuer)
                    createFlags = lsfMPTAuthorized;
            }

            if (auto const err = checkCreateMPT(view, mptIssue, account, {}, createFlags, journal);
                !isTesSuccess(err))
            {
                // checkCreateMPT only fails on tecDIR_FULL (its source line is
                // itself LCOV-excluded) or a missing account, which cannot
                // happen since `account` is the withdrawing LP. Defensive and
                // unreachable in practice.
                return err;  // LCOV_EXCL_LINE
            }
        }
        return tesSUCCESS;
    };

    if (auto const err = sufficientReserve(amountWithdrawActual.asset()))
        return {err, STAmount{}, STAmount{}, STAmount{}};

    if (auto const res = createMPToken(amountWithdrawActual.asset()); !isTesSuccess(res))
        return {res, STAmount{}, STAmount{}, STAmount{}};

    // Withdraw amountWithdraw
    auto res = accountSend(
        view, ammAccount, account, amountWithdrawActual, journal, {}, WaiveTransferFee::Yes);
    if (!isTesSuccess(res))
    {
        // LCOV_EXCL_START
        JLOG(journal.debug()) << "AMM Withdraw: failed to withdraw " << amountWithdrawActual;
        return {res, STAmount{}, STAmount{}, STAmount{}};
        // LCOV_EXCL_STOP
    }

    // Withdraw amount2Withdraw
    if (amount2WithdrawActual)
    {
        if (auto const err = sufficientReserve(amount2WithdrawActual->asset()); !isTesSuccess(err))
            return {err, STAmount{}, STAmount{}, STAmount{}};

        if (auto const res = createMPToken(amount2WithdrawActual->asset()); !isTesSuccess(res))
            return {res, STAmount{}, STAmount{}, STAmount{}};

        res = accountSend(
            view, ammAccount, account, *amount2WithdrawActual, journal, {}, WaiveTransferFee::Yes);
        if (!isTesSuccess(res))
        {
            // LCOV_EXCL_START
            JLOG(journal.debug()) << "AMM Withdraw: failed to withdraw " << *amount2WithdrawActual;
            return {res, STAmount{}, STAmount{}, STAmount{}};
            // LCOV_EXCL_STOP
        }
    }

    // Withdraw LP tokens
    res = redeemIOU(
        view, account, lpTokensWithdrawActual, lpTokensWithdrawActual.get<Issue>(), journal);
    if (!isTesSuccess(res))
    {
        // LCOV_EXCL_START
        JLOG(journal.debug()) << "AMM Withdraw: failed to withdraw LPTokens";
        return {res, STAmount{}, STAmount{}, STAmount{}};
        // LCOV_EXCL_STOP
    }

    return std::make_tuple(
        tesSUCCESS,
        lpTokensAMMBalance - lpTokensWithdrawActual,
        amountWithdrawActual,
        amount2WithdrawActual);
}

static STAmount
adjustLPTokensIn(
    Rules const& rules,
    STAmount const& lptAMMBalance,
    STAmount const& lpTokensWithdraw,
    WithdrawAll withdrawAll)
{
    if (!rules.enabled(fixAMMv1_3) || withdrawAll == WithdrawAll::Yes)
        return lpTokensWithdraw;
    return adjustLPTokens(lptAMMBalance, lpTokensWithdraw, IsDeposit::No);
}

/**
 * Proportional withdrawal of pool assets for the amount of LPTokens.
 */
std::pair<TER, STAmount>
AMMWithdraw::equalWithdrawTokens(
    Sandbox& view,
    SLE const& ammSle,
    AccountID const& ammAccount,
    STAmount const& amountBalance,
    STAmount const& amount2Balance,
    STAmount const& lptAMMBalance,
    STAmount const& lpTokens,
    STAmount const& lpTokensWithdraw,
    std::uint16_t tfee)
{
    TER ter;
    STAmount newLPTokenBalance;
    std::tie(ter, newLPTokenBalance, std::ignore, std::ignore) = equalWithdrawTokens(
        view,
        ammSle,
        accountID_,
        std::nullopt,
        ammAccount,
        amountBalance,
        amount2Balance,
        lptAMMBalance,
        lpTokens,
        lpTokensWithdraw,
        tfee,
        issuerFreezeHandling(),
        AuthHandling::ZeroIfUnauthorized,
        ReserveHandling::EnforceReserve,
        isWithdrawAll(ctx_.tx),
        preFeeBalance_,
        ctx_.journal);
    return {ter, newLPTokenBalance};
}

std::pair<TER, bool>
AMMWithdraw::deleteAMMAccountIfEmpty(
    Sandbox& sb,
    SLE::pointer const ammSle,
    STAmount const& lpTokenBalance,
    Asset const& asset1,
    Asset const& asset2,
    beast::Journal const& journal,
    std::uint8_t curveType)
{
    TER ter;
    bool updateBalance = true;
    if (lpTokenBalance == beast::kZero)
    {
        ter = deleteAMMAccount(sb, asset1, asset2, journal, curveType);
        if (!isTesSuccess(ter) && ter != tecINCOMPLETE)
            return {ter, false};  // LCOV_EXCL_LINE

        updateBalance = (ter == tecINCOMPLETE);
    }

    if (updateBalance)
    {
        ammSle->setFieldAmount(sfLPTokenBalance, lpTokenBalance);
        sb.update(ammSle);
    }

    return {ter, true};
}

/**
 * Proportional withdrawal of pool assets for the amount of LPTokens.
 */
std::tuple<TER, STAmount, STAmount, std::optional<STAmount>>
AMMWithdraw::equalWithdrawTokens(
    Sandbox& view,
    SLE const& ammSle,
    AccountID const account,
    std::optional<AccountID> const& clawbackIssuer,
    AccountID const& ammAccount,
    STAmount const& amountBalance,
    STAmount const& amount2Balance,
    STAmount const& lptAMMBalance,
    STAmount const& lpTokens,
    STAmount const& lpTokensWithdraw,
    std::uint16_t tfee,
    FreezeHandling freezeHandling,
    AuthHandling authHandling,
    ReserveHandling reserveHandling,
    WithdrawAll withdrawAll,
    XRPAmount const& priorBalance,
    beast::Journal const& journal)
{
    try
    {
        // Withdrawing all tokens in the pool
        if (lpTokensWithdraw == lptAMMBalance)
        {
            return withdraw(
                view,
                ammSle,
                ammAccount,
                clawbackIssuer,
                account,
                amountBalance,
                amountBalance,
                amount2Balance,
                lptAMMBalance,
                lpTokensWithdraw,
                tfee,
                freezeHandling,
                authHandling,
                reserveHandling,
                WithdrawAll::Yes,
                priorBalance,
                journal);
        }

        auto const tokensAdj =
            adjustLPTokensIn(view.rules(), lptAMMBalance, lpTokensWithdraw, withdrawAll);
        if (view.rules().enabled(fixAMMv1_3) && tokensAdj == beast::kZero)
            return {tecAMM_INVALID_TOKENS, STAmount{}, STAmount{}, std::nullopt};
        // the adjusted tokens are factored in
        auto const frac = divide(tokensAdj, lptAMMBalance, noIssue());
        auto const amountWithdraw =
            getRoundedAsset(view.rules(), amountBalance, frac, IsDeposit::No);
        auto const amount2Withdraw =
            getRoundedAsset(view.rules(), amount2Balance, frac, IsDeposit::No);
        // LP is making equal withdrawal by tokens but the requested amount
        // of LP tokens is likely too small and results in one-sided pool
        // withdrawal due to round off. Fail so the user withdraws
        // more tokens.
        if (amountWithdraw == beast::kZero || amount2Withdraw == beast::kZero)
            return {tecAMM_FAILED, STAmount{}, STAmount{}, STAmount{}};

        return withdraw(
            view,
            ammSle,
            ammAccount,
            clawbackIssuer,
            account,
            amountBalance,
            amountWithdraw,
            amount2Withdraw,
            lptAMMBalance,
            tokensAdj,
            tfee,
            freezeHandling,
            authHandling,
            reserveHandling,
            withdrawAll,
            priorBalance,
            journal);
    }
    // LCOV_EXCL_START
    catch (std::exception const& e)
    {
        JLOG(journal.error()) << "AMMWithdraw::equalWithdrawTokens exception " << e.what();
    }
    return {tecINTERNAL, STAmount{}, STAmount{}, STAmount{}};
    // LCOV_EXCL_STOP
}

/**
 * All assets withdrawal with the constraints on the maximum amount
 * of each asset that the trader is willing to withdraw.
 *       a = (t/T) * A (5)
 *       b = (t/T) * B (6)
 *       where
 *      A,B: current pool composition
 *      T: current balance of outstanding LPTokens
 *      a: balance of asset A being withdrawn
 *      b: balance of asset B being withdrawn
 *      t: balance of LPTokens issued to LP after a successful transaction
 * Use equation 5 to compute t, given the amount in Asset1Out. Let this be Z
 * Use equation 6 to compute the amount of asset2, given Z. Let
 *     the computed amount of asset2 be X
 * If X <= amount in Asset2Out:
 *   The amount of asset1 to be withdrawn is the one specified in Asset1Out
 *   The amount of asset2 to be withdrawn is X
 *   The amount of LPTokens redeemed is Z
 * If X> amount in Asset2Out:
 *   Use equation 5 to compute t, given the amount in Asset2Out. Let this be Q
 *   Use equation 6 to compute the amount of asset1, given Q.
 *     Let the computed amount of asset1 be W
 *   The amount of asset2 to be withdrawn is the one specified in Asset2Out
 *   The amount of asset1 to be withdrawn is W
 *   The amount of LPTokens redeemed is Q
 */
std::pair<TER, STAmount>
AMMWithdraw::equalWithdrawLimit(
    Sandbox& view,
    SLE const& ammSle,
    AccountID const& ammAccount,
    STAmount const& amountBalance,
    STAmount const& amount2Balance,
    STAmount const& lptAMMBalance,
    STAmount const& amount,
    STAmount const& amount2,
    std::uint16_t tfee)
{
    auto frac = Number{amount} / amountBalance;
    auto tokensAdj = getRoundedLPTokens(view.rules(), lptAMMBalance, frac, IsDeposit::No);
    if (view.rules().enabled(fixAMMv1_3) && tokensAdj == beast::kZero)
        return {tecAMM_INVALID_TOKENS, STAmount{}};
    // factor in the adjusted tokens
    frac = adjustFracByTokens(view.rules(), lptAMMBalance, tokensAdj, frac);
    auto const amount2Withdraw = getRoundedAsset(view.rules(), amount2Balance, frac, IsDeposit::No);
    if (amount2Withdraw <= amount2)
    {
        return withdraw(
            view,
            ammSle,
            ammAccount,
            amountBalance,
            amount,
            amount2Withdraw,
            lptAMMBalance,
            tokensAdj,
            tfee);
    }

    frac = Number{amount2} / amount2Balance;
    auto amountWithdraw = getRoundedAsset(view.rules(), amountBalance, frac, IsDeposit::No);
    tokensAdj = getRoundedLPTokens(view.rules(), lptAMMBalance, frac, IsDeposit::No);
    if (view.rules().enabled(fixAMMv1_3) && tokensAdj == beast::kZero)
        return {tecAMM_INVALID_TOKENS, STAmount{}};  // LCOV_EXCL_LINE
    // factor in the adjusted tokens
    frac = adjustFracByTokens(view.rules(), lptAMMBalance, tokensAdj, frac);
    amountWithdraw = getRoundedAsset(view.rules(), amountBalance, frac, IsDeposit::No);
    if (!view.rules().enabled(fixAMMv1_3))
    {
        // LCOV_EXCL_START
        XRPL_ASSERT(
            amountWithdraw <= amount,
            "xrpl::AMMWithdraw::equalWithdrawLimit : maximum amountWithdraw");
        // LCOV_EXCL_STOP
    }
    else if (amountWithdraw > amount)
    {
        return {tecAMM_FAILED, STAmount{}};  // LCOV_EXCL_LINE
    }
    return withdraw(
        view,
        ammSle,
        ammAccount,
        amountBalance,
        amountWithdraw,
        amount2,
        lptAMMBalance,
        tokensAdj,
        tfee);
}

/**
 * Withdraw single asset equivalent to the amount specified in Asset1Out.
 * t = T * (c - sqrt(c**2 - 4*R))/2
 *     where R = b/B, c = R*fee + 2 - fee
 * Use equation 7 to compute the t, given the amount in Asset1Out.
 */
std::pair<TER, STAmount>
AMMWithdraw::singleWithdraw(
    Sandbox& view,
    SLE const& ammSle,
    AccountID const& ammAccount,
    STAmount const& amountBalance,
    STAmount const& lptAMMBalance,
    STAmount const& amount,
    std::uint16_t tfee)
{
    auto const tokens = adjustLPTokensIn(
        view.rules(),
        lptAMMBalance,
        lpTokensIn(amountBalance, amount, lptAMMBalance, tfee),
        isWithdrawAll(ctx_.tx));
    if (tokens == beast::kZero)
    {
        if (!view.rules().enabled(fixAMMv1_3))
        {
            return {tecAMM_FAILED, STAmount{}};  // LCOV_EXCL_LINE
        }

        return {tecAMM_INVALID_TOKENS, STAmount{}};
    }
    // factor in the adjusted tokens
    auto const [tokensAdj, amountWithdrawAdj] =
        adjustAssetOutByTokens(view.rules(), amountBalance, amount, lptAMMBalance, tokens, tfee);
    if (view.rules().enabled(fixAMMv1_3) && tokensAdj == beast::kZero)
        return {tecAMM_INVALID_TOKENS, STAmount{}};  // LCOV_EXCL_LINE
    return withdraw(
        view,
        ammSle,
        ammAccount,
        amountBalance,
        amountWithdrawAdj,
        std::nullopt,
        lptAMMBalance,
        tokensAdj,
        tfee);
}

/**
 * withdrawal of single asset specified in Asset1Out proportional
 * to the share represented by the amount of LPTokens.
 * Use equation 8 to compute the amount of asset1, given the redeemed t
 *   represented by LPTokens. Let this be Y.
 * If (amount exists for Asset1Out & Y >= amount in Asset1Out) ||
 *       (amount field does not exist for Asset1Out):
 *   The amount of asset out is Y
 *   The amount of LPTokens redeemed is LPTokens
 *  Equation 8 solves equation 7 @see singleWithdraw for b.
 */
std::pair<TER, STAmount>
AMMWithdraw::singleWithdrawTokens(
    Sandbox& view,
    SLE const& ammSle,
    AccountID const& ammAccount,
    STAmount const& amountBalance,
    STAmount const& lptAMMBalance,
    STAmount const& amount,
    STAmount const& lpTokensWithdraw,
    std::uint16_t tfee)
{
    auto const tokensAdj =
        adjustLPTokensIn(view.rules(), lptAMMBalance, lpTokensWithdraw, isWithdrawAll(ctx_.tx));
    if (view.rules().enabled(fixAMMv1_3) && tokensAdj == beast::kZero)
        return {tecAMM_INVALID_TOKENS, STAmount{}};
    // the adjusted tokens are factored in
    auto const amountWithdraw = ammAssetOut(amountBalance, lptAMMBalance, tokensAdj, tfee);
    if (amount == beast::kZero || amountWithdraw >= amount)
    {
        return withdraw(
            view,
            ammSle,
            ammAccount,
            amountBalance,
            amountWithdraw,
            std::nullopt,
            lptAMMBalance,
            tokensAdj,
            tfee);
    }

    return {tecAMM_FAILED, STAmount{}};
}

/**
 * Withdraw single asset with two constraints.
 * a. amount of asset1 if specified (not 0) in Asset1Out specifies the minimum
 *     amount of asset1 that the trader is willing to withdraw.
 * b. The effective price of asset traded out does not exceed the amount
 *     specified in EPrice
 *       The effective price (EP) of a trade is defined as the ratio
 *       of the tokens the trader sold or swapped in (Token B) and
 *       the token they got in return or swapped out (Token A).
 *       EP(B/A) = b/a (III)
 *       b = B * (t1**2 + t1*(f - 2))/(t1*f - 1) (8)
 *           where t1 = t/T
 * Use equations 8 & III and amount in EPrice to compute the two variables:
 *   asset in as LPTokens. Let this be X
 *   asset out as that in Asset1Out. Let this be Y
 * If (amount exists for Asset1Out & Y >= amount in Asset1Out) ||
 *     (amount field does not exist for Asset1Out):
 *   The amount of assetOut is given by Y
 *   The amount of LPTokens is given by X
 */
std::pair<TER, STAmount>
AMMWithdraw::singleWithdrawEPrice(
    Sandbox& view,
    SLE const& ammSle,
    AccountID const& ammAccount,
    STAmount const& amountBalance,
    STAmount const& lptAMMBalance,
    STAmount const& amount,
    STAmount const& ePrice,
    std::uint16_t tfee)
{
    // LPTokens is asset in => E = t / a and formula (8) is:
    // a = A*(t1**2 + t1*(f - 2))/(t1*f - 1)
    // substitute a as t/E =>
    // t/E = A*(t1**2 + t1*(f - 2))/(t1*f - 1), t1=t/T => t = t1*T
    // t1*T/E = A*((t/T)**2 + t*(f - 2)/T)/(t*f/T - 1) =>
    // T/E = A*(t1 + f-2)/(t1*f - 1) =>
    // T*(t1*f - 1) = A*E*(t1 + f - 2) =>
    // t1*T*f - T = t1*A*E + A*E*(f - 2) =>
    // t1*(T*f - A*E) = T + A*E*(f - 2) =>
    // t = T*(T + A*E*(f - 2))/(T*f - A*E)
    Number const ae = amountBalance * ePrice;
    auto const f = getFee(tfee);
    auto const denom = lptAMMBalance * f - ae;
    // fixCleanup3_3_0: guard against division by zero
    // when ePrice == lptAMMBalance*f/amountBalance
    if (view.rules().enabled(fixCleanup3_3_0) && denom == beast::kZero)
        return {tecAMM_FAILED, STAmount{}};
    auto tokNoRoundCb = [&] { return lptAMMBalance * (lptAMMBalance + ae * (f - 2)) / denom; };
    auto tokProdCb = [&] { return (lptAMMBalance + ae * (f - 2)) / denom; };
    auto const tokensAdj =
        getRoundedLPTokens(view.rules(), tokNoRoundCb, lptAMMBalance, tokProdCb, IsDeposit::No);
    if (tokensAdj <= beast::kZero)
    {
        if (!view.rules().enabled(fixAMMv1_3))
        {
            return {tecAMM_FAILED, STAmount{}};
        }

        return {tecAMM_INVALID_TOKENS, STAmount{}};
    }
    auto amtNoRoundCb = [&] { return tokensAdj / ePrice; };
    auto amtProdCb = [&] { return tokensAdj / ePrice; };
    // the adjusted tokens are factored in
    auto const amountWithdraw =
        getRoundedAsset(view.rules(), amtNoRoundCb, amount, amtProdCb, IsDeposit::No);
    if (amount == beast::kZero || amountWithdraw >= amount)
    {
        return withdraw(
            view,
            ammSle,
            ammAccount,
            amountBalance,
            amountWithdraw,
            std::nullopt,
            lptAMMBalance,
            tokensAdj,
            tfee);
    }

    return {tecAMM_FAILED, STAmount{}};
}

WithdrawAll
AMMWithdraw::isWithdrawAll(STTx const& tx)
{
    if ((tx[sfFlags] & (tfWithdrawAll | tfOneAssetWithdrawAll)) != 0u)
        return WithdrawAll::Yes;
    return WithdrawAll::No;
}
void
AMMWithdraw::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
AMMWithdraw::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
