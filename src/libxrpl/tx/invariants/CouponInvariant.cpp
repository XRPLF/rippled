#include <xrpl/tx/invariants/CouponInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/CouponHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>

#include <algorithm>

namespace xrpl {

void
ValidCouponSchedule::visitEntry(bool isDelete, SLE::ConstRef before, SLE::ConstRef after)
{
    // ApplyStateTable::visit passes the stored entry as after for every erase, insert and
    // modify, so after alone gives the type.
    if (!after)
        return;

    if (isDelete)
    {
        if (after->getType() == ltMPTOKEN && after->isFieldPresent(sfCouponAccrued))
            accrualErased_ = true;
        return;
    }

    if (after->getType() == ltCOUPON_SCHEDULE)
    {
        schedules_.emplace_back(before, after);
    }
    else if (before && after->getType() == ltMPTOKEN)
    {
        mptokens_.emplace_back(before, after);
    }
}

bool
ValidCouponSchedule::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    if (!isTesSuccess(result))
        return true;

    if (accrualErased_)
    {
        JLOG(j.fatal()) << "Invariant failed: MPToken deleted with an unclaimed coupon";
        return false;
    }

    auto const txType = tx.getTxnType();
    bool const isPay = txType == ttCOUPON_PAY;
    bool const mayDrainPool = txType == ttCOUPON_CLAIM || txType == ttCOUPON_SCHEDULE_DELETE;

    for (auto const& [before, after] : schedules_)
    {
        if (after->at(sfPoolAmount) < beast::kZero)
        {
            JLOG(j.fatal()) << "Invariant failed: coupon pool is negative";
            return false;
        }

        if (!before)
            continue;

        STAmount const perUnitBefore = before->at(sfAccruedPerUnit);
        STAmount const perUnitAfter = after->at(sfAccruedPerUnit);

        if (perUnitAfter < perUnitBefore)
        {
            JLOG(j.fatal()) << "Invariant failed: coupon accumulator decreased";
            return false;
        }

        if (perUnitAfter != perUnitBefore && !isPay)
        {
            JLOG(j.fatal()) << "Invariant failed: coupon accumulator moved outside CouponPay";
            return false;
        }

        STAmount const poolBefore = before->at(sfPoolAmount);
        STAmount const poolAfter = after->at(sfPoolAmount);

        if (poolAfter > poolBefore && !isPay)
        {
            JLOG(j.fatal()) << "Invariant failed: coupon pool grew outside CouponPay";
            return false;
        }

        if (poolAfter < poolBefore && !mayDrainPool)
        {
            JLOG(j.fatal()) << "Invariant failed: coupon pool shrank outside a claim or delete";
            return false;
        }

        // Every outstanding unit is funded for the step the accumulator took.
        if (auto const issuance =
                view.read(keylet::mptokenIssuance(after->at(sfMPTokenIssuanceID)));
            isPay && issuance)
        {
            NumberRoundModeGuard const mg(Number::RoundingMode::Downward);
            Number const owed = Number(issuance->at(sfOutstandingAmount)) *
                (Number(perUnitAfter) - Number(perUnitBefore));
            if (owed > Number(poolAfter) - Number(poolBefore))
            {
                JLOG(j.fatal()) << "Invariant failed: coupon pool short of the accumulator";
                return false;
            }
        }
    }

    return std::ranges::all_of(mptokens_, [&j, &view](auto const& change) {
        auto const& [before, after] = change;
        auto const indexBefore = before->at(~sfCouponIndex);
        auto const indexAfter = after->at(~sfCouponIndex);

        // Only a holder without units may take an index that is lower or in another asset.
        if (indexBefore && indexAfter && couponHolderUnits(before) != 0 &&
            (indexAfter->asset() != indexBefore->asset() || *indexAfter < *indexBefore))
        {
            JLOG(j.fatal()) << "Invariant failed: holder coupon index moved backwards";
            return false;
        }

        // A dropped index would credit the holder every coupon since the schedule began, so
        // only an index a deleted schedule left may go.
        if (indexBefore && !indexAfter)
        {
            auto const issuance =
                view.read(keylet::mptokenIssuance(after->at(sfMPTokenIssuanceID)));
            if (issuance && issuance->isFlag(lsfMPTCouponSchedule))
            {
                JLOG(j.fatal()) << "Invariant failed: holder coupon index removed";
                return false;
            }
        }

        return true;
    });
}

}  // namespace xrpl
