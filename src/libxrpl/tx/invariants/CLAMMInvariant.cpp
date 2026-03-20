#include <xrpl/tx/invariants/CLAMMInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/CLAMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/tx/transactors/dex/CLAMMHelpers.h>

namespace xrpl {

void
ValidCLAMM::visitEntry(
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    auto const typeAfter = after ? after->getType() : ltANY;
    auto const typeBefore = before ? before->getType() : ltANY;

    if (typeAfter == ltCLAMM || typeBefore == ltCLAMM)
    {
        if (!before && after)
            clammCreated_ = true;
        else if (isDelete)
            clammDeleted_ = true;
        else
            clammModified_ = true;

        // Capture key fields from the after-SLE for safety-net checks
        if (after && typeAfter == ltCLAMM)
        {
            clammFeeTier_ = after->getFieldU8(sfFeeTier);
            if (after->isFieldPresent(sfSqrtPrice))
            {
                auto const sp = after->getFieldH128(sfSqrtPrice);
                clammSqrtPriceZero_ = (sp == base_uint<128>{});
                clammSqrtPriceAfter_ = sp;
            }
            clammCurrentTickAfter_ = after->getFieldI32(sfCurrentTick);
            clammTickSpacing_ = after->getFieldU16(sfTickSpacing);

            // Verify SqrtPrice-tick consistency:
            // tickToSqrtPrice(currentTick) <= sqrtPrice
            //   <= tickToSqrtPrice(currentTick + 1)
            // Tick crossing sets currentTick = nextTick - 1 while
            // sqrtPrice may land exactly on tickToSqrtPrice(nextTick),
            // so the upper bound is inclusive.  A true mismatch would
            // place sqrtPrice strictly above the next tick boundary.
            if (clammSqrtPriceAfter_ && clammCurrentTickAfter_)
            {
                auto const sqrtPrice =
                    clamm::fromSLEField(*clammSqrtPriceAfter_);
                auto const tick = *clammCurrentTickAfter_;
                auto const lower = clamm::tickToSqrtPrice(tick);
                if (sqrtPrice < lower)
                {
                    clammSqrtPriceTickMismatch_ = true;
                }
                else if (tick < CLAMM_MAX_TICK)
                {
                    auto const upper =
                        clamm::tickToSqrtPrice(tick + 1);
                    if (sqrtPrice > upper)
                        clammSqrtPriceTickMismatch_ = true;
                }
            }
        }
    }

    if (typeAfter == ltCLAMM_TICK || typeBefore == ltCLAMM_TICK)
    {
        clammTickChanged_ = true;
        if (!before && after)
            ++clammTicksCreated_;
        if (isDelete)
            ++clammTicksDeleted_;

        // Verify tick alignment for created/modified ticks
        if (after && typeAfter == ltCLAMM_TICK && clammTickSpacing_)
        {
            auto const tickIndex = after->getFieldI32(sfTickIndex);
            if (!isValidCLAMMTick(tickIndex, *clammTickSpacing_))
                clammTickMisaligned_ = true;
        }

        // Non-deleted tick must have LiquidityGross > 0
        if (after && typeAfter == ltCLAMM_TICK && !isDelete)
        {
            auto const lg = after->getFieldH128(sfLiquidityGross);
            if (lg == base_uint<128>{})
                clammTickLiquidityZero_ = true;
        }
    }

    if (typeAfter == ltCLAMM_POSITION || typeBefore == ltCLAMM_POSITION)
    {
        clammPositionChanged_ = true;
        if (!before && after)
            ++clammPositionsCreated_;
        if (isDelete)
            ++clammPositionsDeleted_;

        // Verify position bounds
        if (after && typeAfter == ltCLAMM_POSITION)
        {
            auto const lower = after->getFieldI32(sfLowerTick);
            auto const upper = after->getFieldI32(sfUpperTick);
            if (lower >= upper ||
                lower < CLAMM_MIN_TICK ||
                upper > CLAMM_MAX_TICK)
            {
                clammPositionBadBounds_ = true;
            }

            // Verify tick spacing alignment (M15)
            if (clammTickSpacing_)
            {
                if (!isValidCLAMMTick(lower, *clammTickSpacing_) ||
                    !isValidCLAMMTick(upper, *clammTickSpacing_))
                {
                    clammPositionBadBounds_ = true;
                }
            }
        }
    }

    // Track tick bitmap mutations (M14)
    if (typeAfter == ltCLAMM_TICK_BITMAP ||
        typeBefore == ltCLAMM_TICK_BITMAP)
    {
        clammTickBitmapChanged_ = true;
    }
}

bool
ValidCLAMM::validateValues(beast::Journal const& j) const
{
    // SqrtPrice must be in [minSqrtRatio, maxSqrtRatio)
    if (clammSqrtPriceAfter_)
    {
        auto const sqrtPrice = clamm::fromSLEField(*clammSqrtPriceAfter_);
        if (sqrtPrice < clamm::minSqrtRatio() ||
            sqrtPrice >= clamm::maxSqrtRatio())
        {
            JLOG(j.fatal())
                << "Invariant failed: SqrtPrice out of valid range";
            return false;
        }
    }

    // CurrentTick must be in [MIN_TICK, MAX_TICK]
    if (clammCurrentTickAfter_)
    {
        if (*clammCurrentTickAfter_ < CLAMM_MIN_TICK ||
            *clammCurrentTickAfter_ > CLAMM_MAX_TICK)
        {
            JLOG(j.fatal())
                << "Invariant failed: CurrentTick "
                << *clammCurrentTickAfter_ << " out of valid range";
            return false;
        }
    }

    // FeeTier and TickSpacing must be coherent
    if (clammFeeTier_ && clammTickSpacing_)
    {
        if (!isValidCLAMMFeeTier(*clammFeeTier_) ||
            clammTickSpacing(*clammFeeTier_) != *clammTickSpacing_)
        {
            JLOG(j.fatal())
                << "Invariant failed: FeeTier/TickSpacing mismatch: tier="
                << static_cast<unsigned>(*clammFeeTier_)
                << " spacing=" << *clammTickSpacing_;
            return false;
        }
    }

    // No misaligned ticks
    if (clammTickMisaligned_)
    {
        JLOG(j.fatal())
            << "Invariant failed: tick not aligned to TickSpacing";
        return false;
    }

    // No invalid position bounds
    if (clammPositionBadBounds_)
    {
        JLOG(j.fatal())
            << "Invariant failed: position has invalid tick bounds";
        return false;
    }

    // Non-deleted tick must have non-zero LiquidityGross
    if (clammTickLiquidityZero_)
    {
        JLOG(j.fatal())
            << "Invariant failed: tick exists with zero LiquidityGross";
        return false;
    }

    // SqrtPrice must be consistent with CurrentTick
    if (clammSqrtPriceTickMismatch_)
    {
        JLOG(j.fatal())
            << "Invariant failed: sqrtPriceToTick(SqrtPrice) != CurrentTick";
        return false;
    }

    return true;
}

bool
ValidCLAMM::finalize(
    STTx const& tx,
    TER const tec,
    XRPAmount const fee,
    ReadView const& view,
    beast::Journal const& j)
{
    // If the CLAMM amendment is not enabled, no CLAMM objects should exist
    if (!view.rules().enabled(featureCLAMM))
    {
        if (clammCreated_ || clammModified_ || clammDeleted_ ||
            clammTickChanged_ || clammPositionChanged_)
        {
            JLOG(j.fatal()) << "Invariant failed: CLAMM objects modified "
                               "without amendment enabled";
            return false;
        }
        return true;
    }

    // If the transaction failed, no CLAMM state should have been created
    if (!isTesSuccess(tec))
        return true;

    auto const txType = tx.getTxnType();

    switch (txType)
    {
        case ttCLAMM_CREATE:
            return finalizeCreate(tx, view, j);
        case ttCLAMM_DEPOSIT:
            return finalizeDeposit(tx, view, j);
        case ttCLAMM_WITHDRAW:
            return finalizeWithdraw(tx, view, j);
        case ttCLAMM_SWAP:
            return finalizeSwap(tx, view, j);
        case ttCLAMM_COLLECT_FEES:
            return finalizeCollectFees(tx, view, j);
        case ttCLAMM_VOTE:
            return finalizeVote(tx, view, j);
        case ttCLAMM_BID:
            return finalizeBid(tx, view, j);
        case ttCLAMM_DELETE:
            return finalizeDelete(tx, view, j);
        case ttCLAMM_CLAWBACK:
            return finalizeClawback(tx, view, j);
        default:
            // Non-CLAMM transactions (e.g. Payment, OfferCreate) can modify
            // CLAMM state via path-finding. Validate structural invariants.
            return validateValues(j);
    }
}

bool
ValidCLAMM::finalizeCreate(
    STTx const& tx,
    ReadView const& view,
    beast::Journal const& j) const
{
    if (!clammCreated_)
    {
        JLOG(j.fatal()) << "Invariant failed: CLAMMCreate did not create pool";
        return false;
    }

    // Safety-net: verify FeeTier is valid
    if (clammFeeTier_ && !isValidCLAMMFeeTier(*clammFeeTier_))
    {
        JLOG(j.fatal())
            << "Invariant failed: CLAMMCreate has invalid FeeTier "
            << static_cast<unsigned>(*clammFeeTier_);
        return false;
    }
    return validateValues(j);
}

bool
ValidCLAMM::finalizeDeposit(
    STTx const& tx,
    ReadView const& view,
    beast::Journal const& j) const
{
    // A deposit should create at least one position
    if (clammPositionsCreated_ == 0 && !tx.isFieldPresent(sfNFTokenID))
    {
        JLOG(j.fatal())
            << "Invariant failed: CLAMMDeposit did not create position";
        return false;
    }
    return validateValues(j);
}

bool
ValidCLAMM::finalizeWithdraw(
    STTx const& tx,
    ReadView const& view,
    beast::Journal const& j) const
{
    // Pool must be modified or deleted (auto-delete on last withdrawal)
    if (!clammModified_ && !clammDeleted_)
    {
        JLOG(j.fatal())
            << "Invariant failed: CLAMMWithdraw did not modify pool";
        return false;
    }

    // Position must be either updated (partial) or deleted (full)
    if (!clammPositionChanged_)
    {
        JLOG(j.fatal())
            << "Invariant failed: CLAMMWithdraw did not touch position";
        return false;
    }

    // Withdrawal must not create new positions
    if (clammPositionsCreated_ != 0)
    {
        JLOG(j.fatal())
            << "Invariant failed: CLAMMWithdraw created "
            << clammPositionsCreated_ << " positions";
        return false;
    }

    // At most one position can be deleted per withdrawal
    if (clammPositionsDeleted_ > 1)
    {
        JLOG(j.fatal())
            << "Invariant failed: CLAMMWithdraw deleted "
            << clammPositionsDeleted_ << " positions";
        return false;
    }

    // Tick entries (lower and upper) must be touched
    if (!clammTickChanged_)
    {
        JLOG(j.fatal())
            << "Invariant failed: CLAMMWithdraw did not update ticks";
        return false;
    }

    // Withdrawal must not create new ticks
    if (clammTicksCreated_ != 0)
    {
        JLOG(j.fatal())
            << "Invariant failed: CLAMMWithdraw created "
            << clammTicksCreated_ << " ticks";
        return false;
    }

    // At most 2 ticks deleted (lower and upper when liquidity reaches zero)
    if (clammTicksDeleted_ > 2)
    {
        JLOG(j.fatal())
            << "Invariant failed: CLAMMWithdraw deleted "
            << clammTicksDeleted_ << " ticks";
        return false;
    }

    return validateValues(j);
}

bool
ValidCLAMM::finalizeSwap(
    STTx const& tx,
    ReadView const& view,
    beast::Journal const& j) const
{
    // Swap should modify the pool
    if (!clammModified_)
    {
        JLOG(j.fatal()) << "Invariant failed: CLAMMSwap did not modify pool";
        return false;
    }

    // Safety-net: SqrtPrice must remain positive after swap
    if (clammSqrtPriceZero_)
    {
        JLOG(j.fatal())
            << "Invariant failed: CLAMMSwap resulted in zero SqrtPrice";
        return false;
    }
    return validateValues(j);
}

bool
ValidCLAMM::finalizeCollectFees(
    STTx const& tx,
    ReadView const& view,
    beast::Journal const& j) const
{
    // CollectFees must update the position's fee snapshots
    if (!clammPositionChanged_)
    {
        JLOG(j.fatal())
            << "Invariant failed: CLAMMCollectFees did not update position";
        return false;
    }
    return validateValues(j);
}

bool
ValidCLAMM::finalizeVote(
    STTx const& tx,
    ReadView const& view,
    beast::Journal const& j) const
{
    // Vote must modify the pool (VoteSlots/TradingFee)
    if (!clammModified_)
    {
        JLOG(j.fatal())
            << "Invariant failed: CLAMMVote did not modify pool";
        return false;
    }
    return validateValues(j);
}

bool
ValidCLAMM::finalizeBid(
    STTx const& tx,
    ReadView const& view,
    beast::Journal const& j) const
{
    // Bid must modify the pool (AuctionSlot)
    if (!clammModified_)
    {
        JLOG(j.fatal())
            << "Invariant failed: CLAMMBid did not modify pool";
        return false;
    }
    return validateValues(j);
}

bool
ValidCLAMM::finalizeDelete(
    STTx const& tx,
    ReadView const& view,
    beast::Journal const& j) const
{
    // Delete must not create any new objects
    if (clammPositionsCreated_ != 0 || clammTicksCreated_ != 0)
    {
        JLOG(j.fatal())
            << "Invariant failed: CLAMMDelete created objects";
        return false;
    }
    // No value validation for delete -- pool is being removed
    return true;
}

bool
ValidCLAMM::finalizeClawback(
    STTx const& tx,
    ReadView const& view,
    beast::Journal const& j) const
{
    // Pool must be modified or deleted (auto-delete if last position)
    if (!clammModified_ && !clammDeleted_)
    {
        JLOG(j.fatal())
            << "Invariant failed: CLAMMClawback did not modify pool";
        return false;
    }

    // At least one position must be modified or deleted
    if (!clammPositionChanged_)
    {
        JLOG(j.fatal())
            << "Invariant failed: CLAMMClawback did not touch position";
        return false;
    }

    // Clawback must not create new positions
    if (clammPositionsCreated_ != 0)
    {
        JLOG(j.fatal())
            << "Invariant failed: CLAMMClawback created "
            << clammPositionsCreated_ << " positions";
        return false;
    }

    // Clawback must not create new ticks
    if (clammTicksCreated_ != 0)
    {
        JLOG(j.fatal())
            << "Invariant failed: CLAMMClawback created "
            << clammTicksCreated_ << " ticks";
        return false;
    }

    return validateValues(j);
}

}  // namespace xrpl
