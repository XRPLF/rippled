#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/CLAMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/tx/transactors/dex/CLAMMHelpers.h>
#include <xrpl/tx/transactors/dex/CLAMMSwap.h>

#include <chrono>

namespace xrpl {

bool
CLAMMSwap::checkExtraFeatures(PreflightContext const& ctx)
{
    return clammEnabled(ctx.rules);
}

NotTEC
CLAMMSwap::preflight(PreflightContext const& ctx)
{
    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    // Validate Amount is positive
    auto const amount = ctx.tx[sfAmount];
    if (amount <= beast::zero)
    {
        JLOG(ctx.j.debug()) << "CLAMM Swap: invalid amount.";
        return temBAD_AMOUNT;
    }

    // Validate SqrtPriceLimit if present
    if (ctx.tx.isFieldPresent(sfSqrtPriceLimit))
    {
        auto const limit =
            clamm::fromSLEField(ctx.tx.getFieldH128(sfSqrtPriceLimit));
        if (limit <= clamm::minSqrtRatio() || limit >= clamm::maxSqrtRatio())
        {
            JLOG(ctx.j.debug())
                << "CLAMM Swap: sqrt price limit out of range.";
            return temBAD_AMOUNT;
        }
    }

    return tesSUCCESS;
}

TER
CLAMMSwap::preclaim(PreclaimContext const& ctx)
{
    std::shared_ptr<SLE const> sleClamm;
    if (auto const poolID = resolvePoolID(ctx.tx))
    {
        sleClamm = ctx.view.read(keylet::clamm(*poolID));
        if (!sleClamm)
        {
            JLOG(ctx.j.debug()) << "CLAMM Swap: pool not found.";
            return tecNO_ENTRY;
        }
    }

    // Check if pool assets are frozen
    if (sleClamm)
    {
        auto const accountID = ctx.tx[sfAccount];
        auto const& clammRef = std::as_const(*sleClamm);
        auto const issue0 = clammRef[sfAsset].get<Issue>();
        auto const issue1 = clammRef[sfAsset2].get<Issue>();
        auto const ammAccountID = sleClamm->getAccountID(sfAccount);

        for (auto const& issue : {issue0, issue1})
        {
            if (isXRP(issue))
                continue;

            // Check if pool's trust line is frozen
            if (isFrozen(ctx.view, ammAccountID, issue))
            {
                JLOG(ctx.j.debug())
                    << "CLAMM Swap: pool asset is frozen, " << issue;
                return tecFROZEN;
            }

            // Check if trader's trust line is individually frozen
            if (isIndividualFrozen(
                    ctx.view, accountID, issue.currency, issue.account))
            {
                JLOG(ctx.j.debug())
                    << "CLAMM Swap: trader asset frozen, " << issue;
                return tecFROZEN;
            }
        }
    }

    return tesSUCCESS;
}

TER
CLAMMSwap::doApply()
{
    auto const account = ctx_.tx[sfAccount];
    auto const optPoolID = resolvePoolID(ctx_.tx);
    if (!optPoolID)
    {
        JLOG(j_.debug()) << "CLAMM Swap: no pool identifier provided.";
        return temMALFORMED;
    }
    auto const poolID = *optPoolID;
    auto const amountIn = ctx_.tx[sfAmount];

    Sandbox sb(&ctx_.view());

    // Read pool
    auto const clammKeylet = keylet::clamm(poolID);
    auto sleClamm = sb.peek(clammKeylet);
    if (!sleClamm)
        return tecNO_ENTRY;

    auto const ammAccountID = sleClamm->getAccountID(sfAccount);
    auto const& clammRef = std::as_const(*sleClamm);
    auto const issue0 = clammRef[sfAsset].get<Issue>();
    auto const issue1 = clammRef[sfAsset2].get<Issue>();
    auto const baseTradingFee = sleClamm->getFieldU16(sfTradingFee);
    auto const tickSpacing = sleClamm->getFieldU16(sfTickSpacing);

    // Check if swapper gets discounted fee via auction slot
    std::uint16_t effectiveFee = baseTradingFee;
    if (sleClamm->isFieldPresent(sfAuctionSlot))
    {
        auto const& auctionSlot =
            sleClamm->peekFieldObject(sfAuctionSlot);
        if (auctionSlot.isFieldPresent(sfAccount))
        {
            using namespace std::chrono;
            auto const now = static_cast<std::uint32_t>(
                duration_cast<seconds>(
                    ctx_.view().header().parentCloseTime.time_since_epoch())
                    .count());
            auto const expiration =
                auctionSlot[~sfExpiration].value_or(0u);

            if (now < expiration)
            {
                auto const slotHolder =
                    auctionSlot.getAccountID(sfAccount);
                bool isAuthorized = (account == slotHolder);

                // Check authorized accounts
                if (!isAuthorized &&
                    auctionSlot.isFieldPresent(sfAuthAccounts))
                {
                    for (auto const& obj :
                         auctionSlot.getFieldArray(sfAuthAccounts))
                    {
                        if (obj[sfAccount] == account)
                        {
                            isAuthorized = true;
                            break;
                        }
                    }
                }

                if (isAuthorized)
                {
                    effectiveFee =
                        auctionSlot[~sfDiscountedFee].value_or(0);
                }
            }
        }
    }

    // Determine swap direction
    auto const inputIssue = amountIn.issue();
    bool const zeroForOne = (inputIssue == issue0);

    if (!zeroForOne && inputIssue != issue1)
    {
        JLOG(j_.debug()) << "CLAMM Swap: input asset doesn't match pool.";
        return tecNO_PERMISSION;
    }

    // Optional sqrt price limit
    clamm::uint128 sqrtPriceLimit;
    if (ctx_.tx.isFieldPresent(sfSqrtPriceLimit))
    {
        sqrtPriceLimit =
            clamm::fromSLEField(ctx_.tx.getFieldH128(sfSqrtPriceLimit));
    }
    else
    {
        sqrtPriceLimit =
            zeroForOne ? clamm::minSqrtRatio() + 1 : clamm::maxSqrtRatio() - 1;
    }

    // Validate price limit direction
    auto sqrtPrice =
        clamm::fromSLEField(sleClamm->getFieldH128(sfSqrtPrice));
    if (zeroForOne)
    {
        if (sqrtPriceLimit >= sqrtPrice)
        {
            JLOG(j_.debug())
                << "CLAMM Swap: sqrt price limit must be below current for zeroForOne.";
            return tecPATH_DRY;
        }
    }
    else
    {
        if (sqrtPriceLimit <= sqrtPrice)
        {
            JLOG(j_.debug())
                << "CLAMM Swap: sqrt price limit must be above current for oneForZero.";
            return tecPATH_DRY;
        }
    }

    auto currentTick = sleClamm->getFieldI32(sfCurrentTick);
    auto liquidity =
        clamm::fromSLEField(sleClamm->getFieldH128(sfLiquidityAmount));
    auto feeGrowthGlobal0 =
        clamm::fromSLEField(sleClamm->getFieldH128(sfFeeGrowthGlobal0));
    auto feeGrowthGlobal1 =
        clamm::fromSLEField(sleClamm->getFieldH128(sfFeeGrowthGlobal1));
    auto const initialAmountIn = clamm::extractAmount(amountIn);

    // Execute swap loop via shared helper
    auto const swapResult = clamm::applySwap(
        sb,
        poolID,
        sqrtPrice,
        currentTick,
        liquidity,
        tickSpacing,
        effectiveFee,
        feeGrowthGlobal0,
        feeGrowthGlobal1,
        initialAmountIn,
        zeroForOne,
        sqrtPriceLimit,
        j_);

    auto const totalAmountOut = swapResult.amountOut;
    auto const totalFees = swapResult.totalFees;

    if (totalAmountOut == 0)
    {
        JLOG(j_.debug()) << "CLAMM Swap: zero output.";
        return tecPATH_DRY;
    }

    // Check slippage protection (DeliverMin)
    if (ctx_.tx.isFieldPresent(sfDeliverMin))
    {
        auto const deliverMin = clamm::extractAmount(ctx_.tx[sfDeliverMin]);
        if (totalAmountOut < deliverMin)
        {
            JLOG(j_.debug()) << "CLAMM Swap: slippage exceeded.";
            return tecPATH_PARTIAL;
        }
    }

    // Transfer input tokens from user to pool.
    // SECURITY: accountSend is called before pool state updates below.
    // This ordering is safe because XRPL transactions are atomic (no
    // external callbacks or reentrancy). All operations execute within a
    // Sandbox (sb); if any transfer fails, the function returns early and
    // sb is not applied, rolling back all changes atomically.
    {
        if (swapResult.amountIn > 0)
        {
            auto const inputAmount =
                clamm::makeSTAmount(inputIssue, swapResult.amountIn);
            auto const res = accountSend(
                sb, account, ammAccountID, inputAmount, j_,
                WaiveTransferFee::Yes);
            if (res != tesSUCCESS)
                return res;
        }
    }

    // Transfer output tokens from pool to user
    {
        auto const outputIssue = zeroForOne ? issue1 : issue0;
        auto const outputAmount =
            clamm::makeSTAmount(outputIssue, totalAmountOut);
        auto const res = accountSend(
            sb, ammAccountID, account, outputAmount, j_,
            WaiveTransferFee::Yes);
        if (res != tesSUCCESS)
            return res;
    }

    // Update pool state from swap result
    sleClamm->setFieldH128(
        sfSqrtPrice, clamm::toSLEField(swapResult.finalSqrtPrice));
    sleClamm->setFieldI32(sfCurrentTick, swapResult.finalTick);
    if (swapResult.finalLiquidity > 0)
        sleClamm->setFieldH128(
            sfLiquidityAmount,
            clamm::toSLEField(swapResult.finalLiquidity));
    else
        sleClamm->makeFieldAbsent(sfLiquidityAmount);
    // Always set feeGrowthGlobal -- never make absent (modular counters)
    sleClamm->setFieldH128(sfFeeGrowthGlobal0,
        clamm::toSLEField(swapResult.feeGrowthGlobal0));
    sleClamm->setFieldH128(sfFeeGrowthGlobal1,
        clamm::toSLEField(swapResult.feeGrowthGlobal1));

    sleClamm->setFieldH256(sfPreviousTxnID, ctx_.tx.getTransactionID());
    sleClamm->setFieldU32(sfPreviousTxnLgrSeq, ctx_.view().seq());
    sb.update(sleClamm);

    sb.apply(ctx_.rawView());
    return tesSUCCESS;
}

}  // namespace xrpl
