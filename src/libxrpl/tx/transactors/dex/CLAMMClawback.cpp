#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/CLAMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/nft.h>
#include <xrpl/tx/transactors/dex/CLAMMClawback.h>
#include <xrpl/tx/transactors/dex/CLAMMHelpers.h>
#include <xrpl/tx/transactors/nft/NFTokenUtils.h>

namespace xrpl {

bool
CLAMMClawback::checkExtraFeatures(PreflightContext const& ctx)
{
    return clammEnabled(ctx.rules);
}

std::uint32_t
CLAMMClawback::getFlagsMask(PreflightContext const& ctx)
{
    return tfCLAMMClawbackMask;
}

NotTEC
CLAMMClawback::preflight(PreflightContext const& ctx)
{
    auto const issuer = ctx.tx[sfAccount];
    auto const holder = ctx.tx[sfHolder];

    if (issuer == holder)
    {
        JLOG(ctx.j.trace())
            << "CLAMM Clawback: holder cannot be the same as issuer.";
        return temMALFORMED;
    }

    auto const asset = ctx.tx[sfAsset].get<Issue>();
    auto const asset2 = ctx.tx[sfAsset2].get<Issue>();

    if (isXRP(asset))
        return temMALFORMED;

    if (asset.account != issuer)
    {
        JLOG(ctx.j.trace())
            << "CLAMM Clawback: Asset's account does not match Account.";
        return temMALFORMED;
    }

    auto const flags = ctx.tx.getFlags();
    if ((flags & tfClawTwoAssets) && asset.account != asset2.account)
    {
        JLOG(ctx.j.trace())
            << "CLAMM Clawback: tfClawTwoAssets requires both assets "
               "issued by the same account.";
        return temINVALID_FLAG;
    }

    if (auto const clawAmount = ctx.tx[~sfAmount])
    {
        if (clawAmount->get<Issue>() != asset)
        {
            JLOG(ctx.j.trace())
                << "CLAMM Clawback: Amount issue does not match Asset.";
            return temBAD_AMOUNT;
        }
        if (*clawAmount <= beast::zero)
            return temBAD_AMOUNT;
    }

    return tesSUCCESS;
}

TER
CLAMMClawback::preclaim(PreclaimContext const& ctx)
{
    auto const asset = ctx.tx[sfAsset].get<Issue>();
    auto const asset2 = ctx.tx[sfAsset2].get<Issue>();
    auto const feeTier = ctx.tx[sfFeeTier];

    auto const sleIssuer =
        ctx.view.read(keylet::account(ctx.tx[sfAccount]));
    if (!sleIssuer)
        return terNO_ACCOUNT;  // LCOV_EXCL_LINE

    if (!ctx.view.read(keylet::account(ctx.tx[sfHolder])))
        return terNO_ACCOUNT;

    if (!ctx.view.read(keylet::clamm(asset, asset2, feeTier)))
    {
        JLOG(ctx.j.debug()) << "CLAMM Clawback: pool not found.";
        return tecNO_ENTRY;
    }

    std::uint32_t const issuerFlags = sleIssuer->getFieldU32(sfFlags);
    if (!(issuerFlags & lsfAllowTrustLineClawback) ||
        (issuerFlags & lsfNoFreeze))
    {
        JLOG(ctx.j.debug())
            << "CLAMM Clawback: issuer lacks clawback permission.";
        return tecNO_PERMISSION;
    }

    return tesSUCCESS;
}

TER
CLAMMClawback::doApply()
{
    Sandbox sb(&ctx_.view());

    auto const ter = applyGuts(sb);
    if (ter == tesSUCCESS)
        sb.apply(ctx_.rawView());

    return ter;
}

TER
CLAMMClawback::applyGuts(Sandbox& sb)
{
    auto const issuer = ctx_.tx[sfAccount];
    auto const holder = ctx_.tx[sfHolder];
    auto const asset = ctx_.tx[sfAsset].get<Issue>();
    auto const asset2 = ctx_.tx[sfAsset2].get<Issue>();
    auto const feeTier = ctx_.tx[sfFeeTier];
    std::optional<STAmount> const clawAmount = ctx_.tx[~sfAmount];

    // Resolve pool
    auto const poolID = keylet::clamm(asset, asset2, feeTier).key;
    auto const clammKeylet = keylet::clamm(poolID);
    auto sleClamm = sb.peek(clammKeylet);
    if (!sleClamm)
        return tefINTERNAL;

    auto const ammAccountID = sleClamm->getAccountID(sfAccount);
    auto const& clammRef = std::as_const(*sleClamm);
    auto const issue0 = clammRef[sfAsset].get<Issue>();
    auto const issue1 = clammRef[sfAsset2].get<Issue>();
    auto const currentTick = sleClamm->getFieldI32(sfCurrentTick);
    auto const tickSpacing = sleClamm->getFieldU16(sfTickSpacing);
    auto const feeGrowthGlobal0 =
        clamm::fromSLEField(sleClamm->getFieldH128(sfFeeGrowthGlobal0));
    auto const feeGrowthGlobal1 =
        clamm::fromSLEField(sleClamm->getFieldH128(sfFeeGrowthGlobal1));

    bool const issuerIsAsset0 = (issue0 == asset);

    // Collect holder's positions in this pool (read-only pass)
    struct PosInfo
    {
        uint256 nfTokenID;
        std::int32_t lowerTick;
        std::int32_t upperTick;
        clamm::uint128 liquidity;
        std::uint64_t principal0;
        std::uint64_t principal1;
        std::uint64_t fees0;
        std::uint64_t fees1;
    };

    std::vector<PosInfo> positions;

    forEachItem(
        sb, holder, [&](std::shared_ptr<SLE const> const& sle) {
            if (sle->getType() != ltCLAMM_POSITION)
                return;
            if (sle->getFieldH256(sfPoolID) != poolID)
                return;

            PosInfo info;
            info.nfTokenID = sle->getFieldH256(sfNFTokenID);
            info.lowerTick = sle->getFieldI32(sfLowerTick);
            info.upperTick = sle->getFieldI32(sfUpperTick);
            info.liquidity = clamm::fromSLEField(
                sle->getFieldH128(sfLiquidityAmount));

            auto const sqrtPriceCurrent = clamm::fromSLEField(
                sleClamm->getFieldH128(sfSqrtPrice));
            auto const sqrtPriceLower =
                clamm::tickToSqrtPrice(info.lowerTick);
            auto const sqrtPriceUpper =
                clamm::tickToSqrtPrice(info.upperTick);

            info.principal0 = 0;
            info.principal1 = 0;

            if (sqrtPriceCurrent <= sqrtPriceLower)
            {
                info.principal0 = clamm::getAmount0ForLiquidity(
                    sqrtPriceLower, sqrtPriceUpper, info.liquidity);
            }
            else if (sqrtPriceCurrent < sqrtPriceUpper)
            {
                info.principal0 = clamm::getAmount0ForLiquidity(
                    sqrtPriceCurrent, sqrtPriceUpper, info.liquidity);
                info.principal1 = clamm::getAmount1ForLiquidity(
                    sqrtPriceLower, sqrtPriceCurrent, info.liquidity);
            }
            else
            {
                info.principal1 = clamm::getAmount1ForLiquidity(
                    sqrtPriceLower, sqrtPriceUpper, info.liquidity);
            }

            auto const feeGrowthInside = clamm::computeFeeGrowthInside(
                sb,
                poolID,
                info.lowerTick,
                info.upperTick,
                currentTick,
                feeGrowthGlobal0,
                feeGrowthGlobal1);

            auto const fgi0Last = clamm::fromSLEField(
                sle->getFieldH128(sfFeeGrowthInside0Last));
            auto const fgi1Last = clamm::fromSLEField(
                sle->getFieldH128(sfFeeGrowthInside1Last));

            info.fees0 = sle->getFieldU64(sfTokensOwed0);
            info.fees1 = sle->getFieldU64(sfTokensOwed1);

            if (info.liquidity > 0)
            {
                auto const feeDelta0 =
                    feeGrowthInside.feeGrowthInside0 - fgi0Last;
                auto const feeDelta1 =
                    feeGrowthInside.feeGrowthInside1 - fgi1Last;

                info.fees0 += static_cast<std::uint64_t>(
                    (clamm::uint256(info.liquidity) *
                     clamm::uint256(feeDelta0)) >>
                    clamm::Q96);
                info.fees1 += static_cast<std::uint64_t>(
                    (clamm::uint256(info.liquidity) *
                     clamm::uint256(feeDelta1)) >>
                    clamm::Q96);
            }

            positions.push_back(info);
        });

    if (positions.empty())
        return tecAMM_BALANCE;

    // Compute total issuer's asset across all positions (saturating)
    auto const saturatingAdd = [](std::uint64_t a, std::uint64_t b)
        -> std::uint64_t {
        return (a <= UINT64_MAX - b) ? a + b : UINT64_MAX;
    };
    std::uint64_t totalIssuerAmount = 0;
    for (auto const& pos : positions)
    {
        auto const posAmount = issuerIsAsset0
            ? saturatingAdd(pos.principal0, pos.fees0)
            : saturatingAdd(pos.principal1, pos.fees1);
        totalIssuerAmount = saturatingAdd(totalIssuerAmount, posAmount);
    }

    // Determine target clawback amount
    std::uint64_t const targetAmount = clawAmount
        ? std::min(clamm::extractAmount(*clawAmount), totalIssuerAmount)
        : totalIssuerAmount;

    // Build per-position withdrawal plan:
    // fully withdraw positions until remaining is covered,
    // then partially withdraw the last one if needed.
    struct WithdrawPlan
    {
        clamm::uint128 liquidityToRemove;
        bool fullWithdrawal;
    };
    std::vector<WithdrawPlan> plans(positions.size());

    if (targetAmount >= totalIssuerAmount)
    {
        for (std::size_t i = 0; i < positions.size(); ++i)
            plans[i] = {positions[i].liquidity, true};
    }
    else
    {
        std::uint64_t remaining = targetAmount;
        for (std::size_t i = 0; i < positions.size(); ++i)
        {
            auto const& pos = positions[i];
            auto const posIssuerTotal = issuerIsAsset0
                ? saturatingAdd(pos.principal0, pos.fees0)
                : saturatingAdd(pos.principal1, pos.fees1);

            if (remaining >= posIssuerTotal)
            {
                plans[i] = {pos.liquidity, true};
                remaining -= posIssuerTotal;
            }
            else
            {
                // Partial withdrawal of this position
                auto const posPrincipal =
                    issuerIsAsset0 ? pos.principal0 : pos.principal1;
                auto const posFees =
                    issuerIsAsset0 ? pos.fees0 : pos.fees1;

                clamm::uint128 liqToRemove = 0;
                if (remaining > posFees && posPrincipal > 0)
                {
                    auto const principalNeeded = remaining - posFees;
                    liqToRemove = pos.liquidity *
                        clamm::uint128(principalNeeded) /
                        clamm::uint128(posPrincipal);
                    if (liqToRemove == 0)
                        liqToRemove = 1;
                    if (liqToRemove > pos.liquidity)
                        liqToRemove = pos.liquidity;
                }
                plans[i] = {
                    liqToRemove, liqToRemove == pos.liquidity};
                remaining = 0;
                // Remaining positions: no withdrawal
                for (std::size_t j = i + 1; j < positions.size(); ++j)
                    plans[j] = {clamm::uint128(0), false};
                break;
            }
        }
    }

    // Execute withdrawals and accumulate transfer totals
    std::uint64_t totalTransfer0 = 0;
    std::uint64_t totalTransfer1 = 0;

    auto updateTick =
        [&](std::int32_t tickIndex,
            bool isLower,
            clamm::uint128 const& liquidityToRemove) -> TER {
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
                    keylet::ownerDir(ammAccountID),
                    ownerNode,
                    tickKeylet,
                    true))
            {
                JLOG(j_.debug())
                    << "CLAMM Clawback: dir remove tick failed.";
                return tefBAD_LEDGER;
            }
            sb.erase(sleTick);
            if (auto const ret = clamm::flipTickBitmap(
                    sb,
                    poolID,
                    ammAccountID,
                    tickIndex,
                    tickSpacing,
                    j_);
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

    for (std::size_t i = 0; i < positions.size(); ++i)
    {
        auto const& pos = positions[i];
        auto const& plan = plans[i];

        if (plan.liquidityToRemove == 0 && !plan.fullWithdrawal)
        {
            // Fee-only collection for partial clawback:
            // collect fees without removing liquidity
            auto const posKeylet =
                keylet::clammPosition(pos.nfTokenID);
            auto slePos = sb.peek(posKeylet);
            if (!slePos)
                return tefINTERNAL;

            totalTransfer0 += pos.fees0;
            totalTransfer1 += pos.fees1;

            // Reset fee snapshots
            auto const feeGrowthInside =
                clamm::computeFeeGrowthInside(
                    sb,
                    poolID,
                    pos.lowerTick,
                    pos.upperTick,
                    currentTick,
                    feeGrowthGlobal0,
                    feeGrowthGlobal1);

            // Always update snapshot unconditionally
            slePos->setFieldH128(sfFeeGrowthInside0Last,
                clamm::toSLEField(feeGrowthInside.feeGrowthInside0));
            slePos->setFieldH128(sfFeeGrowthInside1Last,
                clamm::toSLEField(feeGrowthInside.feeGrowthInside1));

            if (slePos->isFieldPresent(sfTokensOwed0))
                slePos->makeFieldAbsent(sfTokensOwed0);
            if (slePos->isFieldPresent(sfTokensOwed1))
                slePos->makeFieldAbsent(sfTokensOwed1);

            slePos->setFieldH256(
                sfPreviousTxnID, ctx_.tx.getTransactionID());
            slePos->setFieldU32(
                sfPreviousTxnLgrSeq, ctx_.view().seq());
            sb.update(slePos);
            continue;
        }

        auto const liquidityToRemove = plan.liquidityToRemove;

        // Recompute principal for the actual liquidity removed
        auto const sqrtPriceCurrent = clamm::fromSLEField(
            sleClamm->getFieldH128(sfSqrtPrice));
        auto const sqrtPriceLower =
            clamm::tickToSqrtPrice(pos.lowerTick);
        auto const sqrtPriceUpper =
            clamm::tickToSqrtPrice(pos.upperTick);

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

        // Include fees (always fully collected), with saturating add
        amount0 = (amount0 <= UINT64_MAX - pos.fees0)
            ? amount0 + pos.fees0
            : UINT64_MAX;
        amount1 = (amount1 <= UINT64_MAX - pos.fees1)
            ? amount1 + pos.fees1
            : UINT64_MAX;

        totalTransfer0 += amount0;
        totalTransfer1 += amount1;

        // Update tick entries
        if (auto const ret =
                updateTick(pos.lowerTick, true, liquidityToRemove);
            ret != tesSUCCESS)
            return ret;
        if (auto const ret =
                updateTick(pos.upperTick, false, liquidityToRemove);
            ret != tesSUCCESS)
            return ret;

        // Update pool's active liquidity if position was in range
        if (pos.lowerTick <= currentTick && currentTick < pos.upperTick)
        {
            auto poolLiquidity = clamm::fromSLEField(
                sleClamm->getFieldH128(sfLiquidityAmount));
            if (poolLiquidity >= liquidityToRemove)
                poolLiquidity -= liquidityToRemove;
            else
                poolLiquidity = 0;
            if (poolLiquidity > 0)
                sleClamm->setFieldH128(
                    sfLiquidityAmount,
                    clamm::toSLEField(poolLiquidity));
            else
                sleClamm->makeFieldAbsent(sfLiquidityAmount);
        }

        // Handle position lifecycle
        auto const posKeylet = keylet::clammPosition(pos.nfTokenID);
        auto slePos = sb.peek(posKeylet);
        if (!slePos)
            return tefINTERNAL;

        if (plan.fullWithdrawal)
        {
            // Remove position from holder's directory
            auto const ownerNode = slePos->getFieldU64(sfOwnerNode);
            if (!sb.dirRemove(
                    keylet::ownerDir(holder),
                    ownerNode,
                    posKeylet,
                    true))
            {
                JLOG(j_.debug())
                    << "CLAMM Clawback: dir remove position failed.";
                return tefBAD_LEDGER;
            }
            sb.erase(slePos);

            // Burn the NFToken
            if (auto const ret =
                    nft::removeToken(sb, holder, pos.nfTokenID);
                ret != tesSUCCESS)
            {
                JLOG(j_.debug())
                    << "CLAMM Clawback: NFToken burn failed: "
                    << transHuman(ret);
                return ret;
            }
        }
        else
        {
            // Partial withdrawal: update position
            auto remaining = pos.liquidity - liquidityToRemove;
            slePos->setFieldH128(
                sfLiquidityAmount, clamm::toSLEField(remaining));

            auto const feeGrowthInside =
                clamm::computeFeeGrowthInside(
                    sb,
                    poolID,
                    pos.lowerTick,
                    pos.upperTick,
                    currentTick,
                    feeGrowthGlobal0,
                    feeGrowthGlobal1);

            // Always update snapshot unconditionally
            slePos->setFieldH128(sfFeeGrowthInside0Last,
                clamm::toSLEField(feeGrowthInside.feeGrowthInside0));
            slePos->setFieldH128(sfFeeGrowthInside1Last,
                clamm::toSLEField(feeGrowthInside.feeGrowthInside1));

            if (slePos->isFieldPresent(sfTokensOwed0))
                slePos->makeFieldAbsent(sfTokensOwed0);
            if (slePos->isFieldPresent(sfTokensOwed1))
                slePos->makeFieldAbsent(sfTokensOwed1);

            slePos->setFieldH256(
                sfPreviousTxnID, ctx_.tx.getTransactionID());
            slePos->setFieldU32(
                sfPreviousTxnLgrSeq, ctx_.view().seq());
            sb.update(slePos);
        }
    }

    // Transfer the issuer's asset directly from pool to issuer (clawback).
    // The paired asset remains in the pool -- sending it to the holder
    // would enrich the clawback target at other LPs' expense.
    {
        auto const issuerTransfer = issuerIsAsset0
            ? totalTransfer0 : totalTransfer1;
        if (issuerTransfer > 0)
        {
            auto const issuerAmount = issuerIsAsset0
                ? clamm::makeSTAmount(issue0, issuerTransfer)
                : clamm::makeSTAmount(issue1, issuerTransfer);
            auto const res = accountSend(
                sb, ammAccountID, issuer, issuerAmount, j_,
                WaiveTransferFee::Yes);
            if (res != tesSUCCESS)
            {
                JLOG(j_.debug())
                    << "CLAMM Clawback: transfer issuer asset failed: "
                    << transHuman(res);
                return res;
            }
        }
    }

    // If tfClawTwoAssets: also send paired asset from pool to issuer.
    // Otherwise the paired asset stays in the pool.
    if (ctx_.tx.getFlags() & tfClawTwoAssets)
    {
        auto const pairedTransfer = issuerIsAsset0
            ? totalTransfer1 : totalTransfer0;
        if (pairedTransfer > 0)
        {
            auto const pairedAmount = issuerIsAsset0
                ? clamm::makeSTAmount(issue1, pairedTransfer)
                : clamm::makeSTAmount(issue0, pairedTransfer);
            auto const res = accountSend(
                sb, ammAccountID, issuer, pairedAmount, j_,
                WaiveTransferFee::Yes);
            if (res != tesSUCCESS)
            {
                JLOG(j_.debug())
                    << "CLAMM Clawback: transfer paired asset failed: "
                    << transHuman(res);
                return res;
            }
        }
    }

    // Auto-delete pool if empty
    bool poolDeleted = false;
    if (!sleClamm->isFieldPresent(sfLiquidityAmount))
    {
        auto const poolDirKeylet = keylet::ownerDir(ammAccountID);

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
                    if (sleItem->getFieldAmount(sfBalance) !=
                        beast::zero)
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

    return tesSUCCESS;
}

}  // namespace xrpl
