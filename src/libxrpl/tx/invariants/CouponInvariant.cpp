#include <xrpl/tx/invariants/CouponInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TxFormats.h>

namespace xrpl {

void
ValidCouponSchedule::visitEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after)
{
    if (isDelete)
        return;

    if (before && before->getType() == ltCOUPON_SCHEDULE)
        schedules_.emplace_back(before, after);
    else if (!before && after && after->getType() == ltCOUPON_SCHEDULE)
        schedules_.emplace_back(before, after);

    if (before && before->getType() == ltMPTOKEN)
        mptokens_.emplace_back(before, after);
}

bool
ValidCouponSchedule::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const,
    ReadView const&,
    beast::Journal const& j)
{
    if (!isTesSuccess(result))
        return true;

    auto const txType = tx.getTxnType();
    bool const isPay = txType == ttCOUPON_PAY;
    bool const mayDrainPool = txType == ttCOUPON_CLAIM || txType == ttCOUPON_SCHEDULE_DELETE;

    for (auto const& [before, after] : schedules_)
    {
        if (!after)
            continue;

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
    }

    for (auto const& [before, after] : mptokens_)
    {
        if (!before || !after)
            continue;

        auto const indexBefore = before->at(~sfCouponIndex);
        auto const indexAfter = after->at(~sfCouponIndex);

        if (indexBefore && indexAfter && *indexAfter < *indexBefore)
        {
            JLOG(j.fatal()) << "Invariant failed: holder coupon index moved backwards";
            return false;
        }

        // A holder's index may not be dropped while it carries an accrual.
        if (indexBefore && !indexAfter && after->isFieldPresent(sfCouponAccrued))
        {
            JLOG(j.fatal()) << "Invariant failed: holder coupon index removed while owed";
            return false;
        }
    }

    return true;
}

}  // namespace xrpl
