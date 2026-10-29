#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <cstdint>

namespace xrpl {

/**
 * Settle a holder's accrued coupons on their MPToken.
 *
 * Adds units * (AccruedPerUnit - CouponIndex) to CouponAccrued, rounded
 * once downward to the coupon asset, then advances CouponIndex to
 * AccruedPerUnit. Units are MPTAmount + LockedAmount. A holder with no
 * units only takes the current index. A pseudo-account accrues nothing.
 * An addition too small to change CouponAccrued at its precision is
 * dropped. Increments the schedule's ClaimantCount when CouponAccrued
 * becomes non-zero on a holder that had none. The caller must
 * view.update() both entries.
 */
void
couponSettleMPToken(ReadView const& view, SLE::Ref schedule, SLE::Ref mptoken, beast::Journal j);

/**
 * Settle a holder against the schedule of the issuance their MPToken
 * belongs to, if that issuance carries lsfMPTCouponSchedule. When the
 * flag is clear it only drops a CouponIndex an earlier schedule left.
 *
 * This is the entry point called from the MPT unit-change helpers.
 */
void
couponSettleIfScheduled(
    ApplyView& view,
    SLE::ConstRef issuance,
    SLE::Ref mptoken,
    beast::Journal j);

/**
 * The units a holder is credited for: MPTAmount + LockedAmount.
 */
[[nodiscard]] std::uint64_t
couponHolderUnits(SLE::ConstRef mptoken);

}  // namespace xrpl
