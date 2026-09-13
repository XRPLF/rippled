#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <utility>
#include <vector>

namespace xrpl {

/**
 * @brief Invariants: a coupon schedule's accumulator and pool only move the
 * ways the amendment allows.
 *
 * 1. `AccruedPerUnit` never decreases, and changes only under `CouponPay`.
 * 2. `PoolAmount` is never negative. It increases only under `CouponPay` and
 *    decreases only under `CouponClaim` or `CouponScheduleDelete`.
 * 3. A holder's `CouponIndex` never decreases: settlement only advances it
 *    toward the schedule's `AccruedPerUnit`.
 */
class ValidCouponSchedule
{
    // Pair is <before, after>; both are needed to check direction of change.
    std::vector<std::pair<SLE::const_pointer, SLE::const_pointer>> schedules_;
    std::vector<std::pair<SLE::const_pointer, SLE::const_pointer>> mptokens_;

public:
    void
    visitEntry(bool, SLE::const_ref, SLE::const_ref);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

}  // namespace xrpl
