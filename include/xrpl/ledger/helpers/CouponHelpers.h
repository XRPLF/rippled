#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>

namespace xrpl {

/**
 * Settle a holder's accrued coupons on their MPToken.
 *
 * Adds units * (AccruedPerUnit - CouponIndex) to CouponAccrued, rounded
 * once downward to the coupon asset, then advances CouponIndex to
 * AccruedPerUnit. Units are MPTAmount + LockedAmount. Increments the
 * schedule's ClaimantCount when CouponAccrued becomes non-zero on a
 * holder that had none. The caller must view.update() both entries on
 * tesSUCCESS.
 */
[[nodiscard]] TER
couponSettleMPToken(SLE::ref schedule, SLE::ref mptoken, beast::Journal j);

/**
 * Settle a holder against the schedule of the issuance their MPToken
 * belongs to, if that issuance carries lsfMPTCouponSchedule. Does
 * nothing when the flag is clear, which is the case for every issuance
 * without a coupon schedule.
 *
 * This is the entry point called from the MPT unit-change helpers.
 */
[[nodiscard]] TER
couponSettleIfScheduled(
    ApplyView& view,
    SLE::const_ref issuance,
    SLE::ref mptoken,
    beast::Journal j);

/**
 * The units a holder is credited for: MPTAmount + LockedAmount.
 */
[[nodiscard]] std::uint64_t
couponHolderUnits(SLE::const_ref mptoken);

}  // namespace xrpl
