#include <xrpl/ledger/helpers/CouponHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>

namespace xrpl {

std::uint64_t
couponHolderUnits(SLE::const_ref mptoken)
{
    std::uint64_t units = mptoken->at(sfMPTAmount);
    if (auto const locked = mptoken->at(~sfLockedAmount))
        units += *locked;
    return units;
}

TER
couponSettleMPToken(SLE::ref schedule, SLE::ref mptoken, beast::Journal j)
{
    STAmount const perUnit = schedule->at(sfAccruedPerUnit);
    STAmount const zero{perUnit.asset(), 0};
    auto const indexField = mptoken->at(~sfCouponIndex);
    STAmount const index = indexField ? *indexField : zero;

    // Nothing declared since this holder last settled.
    if (perUnit <= index)
        return tesSUCCESS;

    std::uint64_t const units = couponHolderUnits(mptoken);
    if (units != 0)
    {
        auto const accruedField = mptoken->at(~sfCouponAccrued);
        STAmount const accrued = accruedField ? *accruedField : zero;

        NumberRoundModeGuard const mg(Number::RoundingMode::Downward);
        Number const addition = Number(units) * (Number(perUnit) - Number(index));
        STAmount const additionAmt{perUnit.asset(), addition};

        if (!canAdd(accrued, additionAmt))
        {
            JLOG(j.debug()) << "couponSettleMPToken: accrual addition loses precision.";
            return tecPRECISION_LOSS;
        }

        bool const wasEmpty = accrued == beast::kZero;
        STAmount const settled = accrued + additionAmt;
        mptoken->at(sfCouponAccrued) = settled;

        // ClaimantCount tracks MPTokens holding an unclaimed balance.
        if (wasEmpty && settled != beast::kZero)
            schedule->at(sfClaimantCount) = schedule->at(sfClaimantCount) + 1;
    }

    mptoken->at(sfCouponIndex) = perUnit;
    return tesSUCCESS;
}

TER
couponSettleIfScheduled(
    ApplyView& view,
    SLE::const_ref issuance,
    SLE::ref mptoken,
    beast::Journal j)
{
    if (!issuance || !issuance->isFlag(lsfMPTCouponSchedule))
        return tesSUCCESS;

    auto const sleSchedule = view.peek(keylet::couponSchedule(mptoken->at(sfMPTokenIssuanceID)));
    if (!sleSchedule)
        return tesSUCCESS;

    if (auto const ter = couponSettleMPToken(sleSchedule, mptoken, j); !isTesSuccess(ter))
        return ter;

    view.update(sleSchedule);
    return tesSUCCESS;
}

}  // namespace xrpl
