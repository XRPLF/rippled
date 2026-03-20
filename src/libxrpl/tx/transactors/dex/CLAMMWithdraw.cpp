#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/CLAMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/nft.h>
#include <xrpl/tx/transactors/dex/CLAMMHelpers.h>
#include <xrpl/tx/transactors/dex/CLAMMWithdraw.h>
#include <xrpl/tx/transactors/nft/NFTokenUtils.h>

namespace xrpl {

bool
CLAMMWithdraw::checkExtraFeatures(PreflightContext const& ctx)
{
    return clammEnabled(ctx.rules);
}

NotTEC
CLAMMWithdraw::preflight(PreflightContext const& ctx)
{
    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;
    return tesSUCCESS;
}

TER
CLAMMWithdraw::preclaim(PreclaimContext const& ctx)
{
    auto const nfTokenID = ctx.tx.getFieldH256(sfNFTokenID);
    auto const slePos = ctx.view.read(keylet::clammPosition(nfTokenID));
    if (!slePos)
    {
        JLOG(ctx.j.debug()) << "CLAMM Withdraw: position not found.";
        return tecNO_ENTRY;
    }

    // Check if withdrawer's trust line is individually frozen
    auto const poolID = slePos->getFieldH256(sfPoolID);
    auto const sleClamm = ctx.view.read(keylet::clamm(poolID));
    if (sleClamm)
    {
        auto const accountID = ctx.tx[sfAccount];
        auto const& clammRef = std::as_const(*sleClamm);
        auto const issue0 = clammRef[sfAsset].get<Issue>();
        auto const issue1 = clammRef[sfAsset2].get<Issue>();

        for (auto const& issue : {issue0, issue1})
        {
            if (isXRP(issue))
                continue;

            if (isIndividualFrozen(
                    ctx.view, accountID, issue.currency, issue.account))
            {
                JLOG(ctx.j.debug())
                    << "CLAMM Withdraw: withdrawer asset frozen, " << issue;
                return tecFROZEN;
            }
        }
    }

    return tesSUCCESS;
}

TER
CLAMMWithdraw::doApply()
{
    auto const account = ctx_.tx[sfAccount];
    auto const nfTokenID = ctx_.tx.getFieldH256(sfNFTokenID);

    Sandbox sb(&ctx_.view());

    // Read position
    auto const posKeylet = keylet::clammPosition(nfTokenID);
    auto slePos = sb.peek(posKeylet);
    if (!slePos)
        return tecNO_ENTRY;

    // Verify caller owns the NFToken
    auto const posOwner = slePos->getAccountID(sfOwner);
    if (posOwner != account)
    {
        JLOG(j_.debug()) << "CLAMM Withdraw: caller does not own position.";
        return tecNO_PERMISSION;
    }

    auto const poolID = slePos->getFieldH256(sfPoolID);
    auto const lowerTick = slePos->getFieldI32(sfLowerTick);
    auto const upperTick = slePos->getFieldI32(sfUpperTick);
    auto const posLiquidity =
        clamm::fromSLEField(slePos->getFieldH128(sfLiquidityAmount));

    // Read pool
    auto const clammKeylet = keylet::clamm(poolID);
    auto sleClamm = sb.peek(clammKeylet);
    if (!sleClamm)
        return tefINTERNAL;

    auto const ammAccountID = sleClamm->getAccountID(sfAccount);
    auto const currentTick = sleClamm->getFieldI32(sfCurrentTick);
    auto const tickSpacing = sleClamm->getFieldU16(sfTickSpacing);
    auto const feeGrowthGlobal0 =
        clamm::fromSLEField(sleClamm->getFieldH128(sfFeeGrowthGlobal0));
    auto const feeGrowthGlobal1 =
        clamm::fromSLEField(sleClamm->getFieldH128(sfFeeGrowthGlobal1));

    // Determine how much liquidity to remove
    clamm::uint128 liquidityToRemove;
    if (ctx_.tx.isFieldPresent(sfLiquidityAmount))
    {
        liquidityToRemove =
            clamm::fromSLEField(ctx_.tx.getFieldH128(sfLiquidityAmount));
        if (liquidityToRemove > posLiquidity)
            liquidityToRemove = posLiquidity;
    }
    else
    {
        liquidityToRemove = posLiquidity;
    }

    if (liquidityToRemove == 0)
        return tecINSUFFICIENT_PAYMENT;

    // Calculate token amounts to return
    auto const sqrtPriceCurrent =
        clamm::fromSLEField(sleClamm->getFieldH128(sfSqrtPrice));
    auto const sqrtPriceLower = clamm::tickToSqrtPrice(lowerTick);
    auto const sqrtPriceUpper = clamm::tickToSqrtPrice(upperTick);

    std::uint64_t amount0 = 0;
    std::uint64_t amount1 = 0;

    if (sqrtPriceCurrent <= sqrtPriceLower)
    {
        amount0 = clamm::getAmount0ForLiquidity(
            sqrtPriceLower, sqrtPriceUpper, liquidityToRemove);
    }
    else if (sqrtPriceCurrent < sqrtPriceUpper)
    {
        amount0 = clamm::getAmount0ForLiquidity(
            sqrtPriceCurrent, sqrtPriceUpper, liquidityToRemove);
        amount1 = clamm::getAmount1ForLiquidity(
            sqrtPriceLower, sqrtPriceCurrent, liquidityToRemove);
    }
    else
    {
        amount1 = clamm::getAmount1ForLiquidity(
            sqrtPriceLower, sqrtPriceUpper, liquidityToRemove);
    }

    // Compute accrued fees using proper feeGrowthInside
    auto const feeGrowthInside = clamm::computeFeeGrowthInside(
        sb, poolID, lowerTick, upperTick, currentTick,
        feeGrowthGlobal0, feeGrowthGlobal1);

    auto const fgi0Last = clamm::fromSLEField(
        slePos->getFieldH128(sfFeeGrowthInside0Last));
    auto const fgi1Last = clamm::fromSLEField(
        slePos->getFieldH128(sfFeeGrowthInside1Last));

    std::uint64_t feesOwed0 = slePos->getFieldU64(sfTokensOwed0);
    std::uint64_t feesOwed1 = slePos->getFieldU64(sfTokensOwed1);

    if (posLiquidity > 0)
    {
        // Use unsigned wrapping subtraction (modular arithmetic).
        // No >= guard needed: current - last is always the correct delta.
        auto const feeDelta0 =
            feeGrowthInside.feeGrowthInside0 - fgi0Last;
        auto const feeDelta1 =
            feeGrowthInside.feeGrowthInside1 - fgi1Last;

        {
            auto const a = static_cast<std::uint64_t>(
                (clamm::uint256(posLiquidity) * clamm::uint256(feeDelta0)) >>
                clamm::Q96);
            feesOwed0 = (feesOwed0 <= UINT64_MAX - a)
                ? feesOwed0 + a : UINT64_MAX;
        }
        {
            auto const a = static_cast<std::uint64_t>(
                (clamm::uint256(posLiquidity) * clamm::uint256(feeDelta1)) >>
                clamm::Q96);
            feesOwed1 = (feesOwed1 <= UINT64_MAX - a)
                ? feesOwed1 + a : UINT64_MAX;
        }
    }

    // Total amounts to transfer (principal + fees), with saturating add
    amount0 = (amount0 <= UINT64_MAX - feesOwed0)
        ? amount0 + feesOwed0
        : UINT64_MAX;
    amount1 = (amount1 <= UINT64_MAX - feesOwed1)
        ? amount1 + feesOwed1
        : UINT64_MAX;

    // Check MinAmount/MinAmount2 slippage protection
    if (ctx_.tx.isFieldPresent(sfMinAmount))
    {
        auto const minAmount0 = clamm::extractAmount(ctx_.tx[sfMinAmount]);
        if (amount0 < minAmount0)
        {
            JLOG(j_.debug()) << "CLAMM Withdraw: amount0 below minimum.";
            return tecPATH_PARTIAL;
        }
    }
    if (ctx_.tx.isFieldPresent(sfMinAmount2))
    {
        auto const minAmount1 = clamm::extractAmount(ctx_.tx[sfMinAmount2]);
        if (amount1 < minAmount1)
        {
            JLOG(j_.debug()) << "CLAMM Withdraw: amount1 below minimum.";
            return tecPATH_PARTIAL;
        }
    }

    auto const& clammRef = std::as_const(*sleClamm);
    auto const issue0 = clammRef[sfAsset].get<Issue>();
    auto const issue1 = clammRef[sfAsset2].get<Issue>();

    // Transfer tokens from pool to user
    if (amount0 > 0)
    {
        auto const withdraw0 = clamm::makeSTAmount(issue0, amount0);
        auto const res = accountSend(
            sb, ammAccountID, account, withdraw0, j_, WaiveTransferFee::Yes);
        if (res != tesSUCCESS)
        {
            JLOG(j_.debug()) << "CLAMM Withdraw: transfer token0 failed: "
                             << transHuman(res);
            return res;
        }
    }

    if (amount1 > 0)
    {
        auto const withdraw1 = clamm::makeSTAmount(issue1, amount1);
        auto const res = accountSend(
            sb, ammAccountID, account, withdraw1, j_, WaiveTransferFee::Yes);
        if (res != tesSUCCESS)
        {
            JLOG(j_.debug()) << "CLAMM Withdraw: transfer token1 failed: "
                             << transHuman(res);
            return res;
        }
    }

    // Update tick entries
    auto updateTick = [&](std::int32_t tickIndex, bool isLower) -> TER {
        auto const tickKeylet = keylet::clammTick(poolID, tickIndex);
        auto sleTick = sb.peek(tickKeylet);
        if (!sleTick)
            return tefINTERNAL;

        auto gross =
            clamm::fromSLEField(sleTick->getFieldH128(sfLiquidityGross));
        auto net = clamm::fromSLEFieldSigned(
            sleTick->getFieldH128(sfLiquidityNet));

        gross -= liquidityToRemove;

        if (isLower)
            net -= static_cast<clamm::int128>(liquidityToRemove);
        else
            net += static_cast<clamm::int128>(liquidityToRemove);

        if (gross == 0)
        {
            auto const ownerNode = sleTick->getFieldU64(sfOwnerNode);
            if (!sb.dirRemove(
                    keylet::ownerDir(ammAccountID), ownerNode, tickKeylet, true))
            {
                JLOG(j_.debug()) << "CLAMM Withdraw: dir remove tick failed.";
                return tefBAD_LEDGER;
            }
            sb.erase(sleTick);
            if (auto const ret = clamm::flipTickBitmap(
                    sb, poolID, ammAccountID, tickIndex, tickSpacing, j_);
                ret != tesSUCCESS)
                return ret;
        }
        else
        {
            sleTick->setFieldH128(
                sfLiquidityGross, clamm::toSLEField(gross));
            sleTick->setFieldH128(
                sfLiquidityNet, clamm::toSLEFieldSigned(net));
            sleTick->setFieldH256(
                sfPreviousTxnID, ctx_.tx.getTransactionID());
            sleTick->setFieldU32(
                sfPreviousTxnLgrSeq, ctx_.view().seq());
            sb.update(sleTick);
        }
        return tesSUCCESS;
    };

    if (auto const ret = updateTick(lowerTick, true); ret != tesSUCCESS)
        return ret;
    if (auto const ret = updateTick(upperTick, false); ret != tesSUCCESS)
        return ret;

    // Update pool's active liquidity if position was in range
    if (lowerTick <= currentTick && currentTick < upperTick)
    {
        auto poolLiquidity =
            clamm::fromSLEField(sleClamm->getFieldH128(sfLiquidityAmount));
        if (poolLiquidity >= liquidityToRemove)
            poolLiquidity -= liquidityToRemove;
        else
            poolLiquidity = 0;
        if (poolLiquidity > 0)
            sleClamm->setFieldH128(
                sfLiquidityAmount, clamm::toSLEField(poolLiquidity));
        else
            sleClamm->makeFieldAbsent(sfLiquidityAmount);
    }

    bool const fullWithdrawal = (liquidityToRemove == posLiquidity);

    if (fullWithdrawal)
    {
        // Remove position from owner directory
        auto const ownerNode = slePos->getFieldU64(sfOwnerNode);
        if (!sb.dirRemove(
                keylet::ownerDir(account), ownerNode, posKeylet, true))
        {
            JLOG(j_.debug()) << "CLAMM Withdraw: dir remove position failed.";
            return tefBAD_LEDGER;
        }
        sb.erase(slePos);

        // Burn the NFToken
        if (auto const ret = nft::removeToken(sb, account, nfTokenID);
            ret != tesSUCCESS)
        {
            JLOG(j_.debug()) << "CLAMM Withdraw: NFToken burn failed: "
                             << transHuman(ret);
            return ret;
        }
    }
    else
    {
        // Partial withdrawal — update position
        auto remaining = posLiquidity - liquidityToRemove;
        slePos->setFieldH128(
            sfLiquidityAmount, clamm::toSLEField(remaining));

        // Reset fee snapshots unconditionally
        slePos->setFieldH128(sfFeeGrowthInside0Last,
            clamm::toSLEField(feeGrowthInside.feeGrowthInside0));
        slePos->setFieldH128(sfFeeGrowthInside1Last,
            clamm::toSLEField(feeGrowthInside.feeGrowthInside1));

        if (slePos->isFieldPresent(sfTokensOwed0))
            slePos->makeFieldAbsent(sfTokensOwed0);
        if (slePos->isFieldPresent(sfTokensOwed1))
            slePos->makeFieldAbsent(sfTokensOwed1);

        slePos->setFieldH256(sfPreviousTxnID, ctx_.tx.getTransactionID());
        slePos->setFieldU32(sfPreviousTxnLgrSeq, ctx_.view().seq());
        sb.update(slePos);
    }

    // Auto-delete pool if empty after full withdrawal.
    // Pattern: similar to AMMWithdraw::deleteAMMAccountIfEmpty().
    // Pool is empty when active liquidity is zero and the pool
    // pseudo-account's directory has no remaining ticks or bitmaps
    // (only zero-balance trust lines may remain).
    bool poolDeleted = false;
    if (fullWithdrawal && !sleClamm->isFieldPresent(sfLiquidityAmount))
    {
        auto const poolDirKeylet = keylet::ownerDir(ammAccountID);

        // Delete zero-balance trust lines in the pool directory.
        // Skip any ticks or bitmaps (indicate pool is NOT empty).
        bool hasNonTrustEntries = false;
        auto const delTer = cleanupOnAccountDelete(
            sb,
            poolDirKeylet,
            [&](LedgerEntryType nodeType,
                uint256 const&,
                std::shared_ptr<SLE>& sleItem)
                -> std::pair<TER, SkipEntry> {
                if (nodeType == ltCLAMM_TICK ||
                    nodeType == ltCLAMM_TICK_BITMAP)
                {
                    hasNonTrustEntries = true;
                    return {tesSUCCESS, SkipEntry::Yes};
                }
                if (nodeType == ltRIPPLE_STATE)
                {
                    if (sleItem->getFieldAmount(sfBalance) != beast::zero)
                    {
                        hasNonTrustEntries = true;
                        return {tesSUCCESS, SkipEntry::Yes};
                    }
                    return {
                        deleteAMMTrustLine(
                            sb, sleItem, ammAccountID, j_),
                        SkipEntry::No};
                }
                return {tesSUCCESS, SkipEntry::Yes};
            },
            j_,
            512);

        if (!hasNonTrustEntries && delTer == tesSUCCESS)
        {
            // No ticks or bitmaps remain. The only directory entry left
            // should be the CLAMM SLE itself (in the pseudo-account's
            // directory since CLAMMCreate). Remove it and delete the pool.
            auto const clammOwnerNode =
                sleClamm->getFieldU64(sfOwnerNode);

            if (sb.dirRemove(
                    keylet::ownerDir(ammAccountID),
                    clammOwnerNode,
                    clammKeylet,
                    false))
            {
                if (sb.exists(poolDirKeylet))
                    sb.emptyDirDelete(poolDirKeylet);

                auto sleAMMRoot =
                    sb.peek(keylet::account(ammAccountID));
                sb.erase(sleClamm);
                if (sleAMMRoot)
                    sb.erase(sleAMMRoot);

                poolDeleted = true;
            }
        }
    }

    if (!poolDeleted)
    {
        sleClamm->setFieldH256(
            sfPreviousTxnID, ctx_.tx.getTransactionID());
        sleClamm->setFieldU32(
            sfPreviousTxnLgrSeq, ctx_.view().seq());
        sb.update(sleClamm);
    }

    sb.apply(ctx_.rawView());
    return tesSUCCESS;
}

}  // namespace xrpl
