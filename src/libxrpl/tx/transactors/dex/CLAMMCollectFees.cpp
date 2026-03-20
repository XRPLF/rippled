#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/CLAMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/tx/transactors/dex/CLAMMCollectFees.h>
#include <xrpl/tx/transactors/dex/CLAMMHelpers.h>

namespace xrpl {

bool
CLAMMCollectFees::checkExtraFeatures(PreflightContext const& ctx)
{
    return clammEnabled(ctx.rules);
}

NotTEC
CLAMMCollectFees::preflight(PreflightContext const& ctx)
{
    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;
    return tesSUCCESS;
}

TER
CLAMMCollectFees::preclaim(PreclaimContext const& ctx)
{
    auto const nfTokenID = ctx.tx.getFieldH256(sfNFTokenID);
    auto const slePos = ctx.view.read(keylet::clammPosition(nfTokenID));
    if (!slePos)
    {
        JLOG(ctx.j.debug()) << "CLAMM CollectFees: position not found.";
        return tecNO_ENTRY;
    }

    // Check if fee collector's trust line is individually frozen
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
                    << "CLAMM CollectFees: collector asset frozen, "
                    << issue;
                return tecFROZEN;
            }
        }
    }

    return tesSUCCESS;
}

TER
CLAMMCollectFees::doApply()
{
    auto const account = ctx_.tx[sfAccount];
    auto const nfTokenID = ctx_.tx.getFieldH256(sfNFTokenID);

    Sandbox sb(&ctx_.view());

    // Read position
    auto const posKeylet = keylet::clammPosition(nfTokenID);
    auto slePos = sb.peek(posKeylet);
    if (!slePos)
        return tecNO_ENTRY;

    auto const posOwner = slePos->getAccountID(sfOwner);
    if (posOwner != account)
    {
        JLOG(j_.debug()) << "CLAMM CollectFees: caller does not own position.";
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
    auto const& clammRef = std::as_const(*sleClamm);
    auto const issue0 = clammRef[sfAsset].get<Issue>();
    auto const issue1 = clammRef[sfAsset2].get<Issue>();
    auto const currentTick = sleClamm->getFieldI32(sfCurrentTick);
    auto const feeGrowthGlobal0 =
        clamm::fromSLEField(sleClamm->getFieldH128(sfFeeGrowthGlobal0));
    auto const feeGrowthGlobal1 =
        clamm::fromSLEField(sleClamm->getFieldH128(sfFeeGrowthGlobal1));

    // Compute feeGrowthInside using shared helper
    auto const feeGrowthInside = clamm::computeFeeGrowthInside(
        sb, poolID, lowerTick, upperTick, currentTick,
        feeGrowthGlobal0, feeGrowthGlobal1);

    auto const fgi0Last = clamm::fromSLEField(
        slePos->getFieldH128(sfFeeGrowthInside0Last));
    auto const fgi1Last = clamm::fromSLEField(
        slePos->getFieldH128(sfFeeGrowthInside1Last));

    // Start with previously accumulated but uncollected fees
    std::uint64_t fees0 = slePos->getFieldU64(sfTokensOwed0);
    std::uint64_t fees1 = slePos->getFieldU64(sfTokensOwed1);

    // Add newly accrued fees
    if (posLiquidity > 0)
    {
        auto const delta0 = feeGrowthInside.feeGrowthInside0 - fgi0Last;
        auto const delta1 = feeGrowthInside.feeGrowthInside1 - fgi1Last;

        if (delta0 > 0)
        {
            auto const a = static_cast<std::uint64_t>(
                (clamm::uint256(posLiquidity) * clamm::uint256(delta0)) >>
                clamm::Q96);
            fees0 = (fees0 <= UINT64_MAX - a) ? fees0 + a : UINT64_MAX;
        }
        if (delta1 > 0)
        {
            auto const a = static_cast<std::uint64_t>(
                (clamm::uint256(posLiquidity) * clamm::uint256(delta1)) >>
                clamm::Q96);
            fees1 = (fees1 <= UINT64_MAX - a) ? fees1 + a : UINT64_MAX;
        }
    }

    // Apply MaxAmount/MaxAmount2 capping
    if (ctx_.tx.isFieldPresent(sfMaxAmount))
    {
        auto const maxAmount0 = clamm::extractAmount(ctx_.tx[sfMaxAmount]);
        if (fees0 > maxAmount0)
            fees0 = maxAmount0;
    }
    if (ctx_.tx.isFieldPresent(sfMaxAmount2))
    {
        auto const maxAmount1 = clamm::extractAmount(ctx_.tx[sfMaxAmount2]);
        if (fees1 > maxAmount1)
            fees1 = maxAmount1;
    }

    if (fees0 == 0 && fees1 == 0)
    {
        JLOG(j_.debug()) << "CLAMM CollectFees: no fees to collect.";
        return tecAMM_EMPTY;
    }

    // Transfer fees from pool to position owner
    if (fees0 > 0)
    {
        auto const feeAmount0 = clamm::makeSTAmount(issue0, fees0);
        auto const res = accountSend(
            sb, ammAccountID, account, feeAmount0, j_, WaiveTransferFee::Yes);
        if (res != tesSUCCESS)
        {
            JLOG(j_.debug())
                << "CLAMM CollectFees: transfer fees0 failed: "
                << transHuman(res);
            return res;
        }
    }

    if (fees1 > 0)
    {
        auto const feeAmount1 = clamm::makeSTAmount(issue1, fees1);
        auto const res = accountSend(
            sb, ammAccountID, account, feeAmount1, j_, WaiveTransferFee::Yes);
        if (res != tesSUCCESS)
        {
            JLOG(j_.debug())
                << "CLAMM CollectFees: transfer fees1 failed: "
                << transHuman(res);
            return res;
        }
    }

    // Update position: snapshot fee growth unconditionally, clear tokensOwed
    slePos->setFieldH128(sfFeeGrowthInside0Last,
        clamm::toSLEField(feeGrowthInside.feeGrowthInside0));
    slePos->setFieldH128(sfFeeGrowthInside1Last,
        clamm::toSLEField(feeGrowthInside.feeGrowthInside1));

    // If MaxAmount capped the withdrawal, store remaining as tokensOwed
    {
        std::uint64_t remaining0 = 0;
        std::uint64_t remaining1 = 0;

        // Recompute total to check if capping occurred
        std::uint64_t totalFees0 = slePos->getFieldU64(sfTokensOwed0);
        std::uint64_t totalFees1 = slePos->getFieldU64(sfTokensOwed1);
        if (posLiquidity > 0)
        {
            auto const delta0 = feeGrowthInside.feeGrowthInside0 - fgi0Last;
            auto const delta1 = feeGrowthInside.feeGrowthInside1 - fgi1Last;
            if (delta0 > 0)
            {
                auto const a = static_cast<std::uint64_t>(
                    (clamm::uint256(posLiquidity) * clamm::uint256(delta0)) >>
                    clamm::Q96);
                totalFees0 = (totalFees0 <= UINT64_MAX - a)
                    ? totalFees0 + a : UINT64_MAX;
            }
            if (delta1 > 0)
            {
                auto const a = static_cast<std::uint64_t>(
                    (clamm::uint256(posLiquidity) * clamm::uint256(delta1)) >>
                    clamm::Q96);
                totalFees1 = (totalFees1 <= UINT64_MAX - a)
                    ? totalFees1 + a : UINT64_MAX;
            }
        }
        remaining0 = totalFees0 - fees0;
        remaining1 = totalFees1 - fees1;

        if (remaining0 > 0)
            slePos->setFieldU64(sfTokensOwed0, remaining0);
        else if (slePos->isFieldPresent(sfTokensOwed0))
            slePos->makeFieldAbsent(sfTokensOwed0);
        if (remaining1 > 0)
            slePos->setFieldU64(sfTokensOwed1, remaining1);
        else if (slePos->isFieldPresent(sfTokensOwed1))
            slePos->makeFieldAbsent(sfTokensOwed1);
    }

    slePos->setFieldH256(sfPreviousTxnID, ctx_.tx.getTransactionID());
    slePos->setFieldU32(sfPreviousTxnLgrSeq, ctx_.view().seq());
    sb.update(slePos);

    sb.apply(ctx_.rawView());
    return tesSUCCESS;
}

}  // namespace xrpl
