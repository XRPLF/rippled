#include <xrpl/ledger/helpers/CouponHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <cstdint>

namespace xrpl {

std::uint64_t
couponHolderUnits(SLE::ConstRef mptoken)
{
    std::uint64_t units = mptoken->at(sfMPTAmount);
    if (auto const locked = mptoken->at(~sfLockedAmount))
        units += *locked;
    return units;
}

void
couponSettleMPToken(ReadView const& view, SLE::Ref schedule, SLE::Ref mptoken, beast::Journal j)
{
    STAmount const perUnit = schedule->at(sfAccruedPerUnit);
    std::uint64_t const units = couponHolderUnits(mptoken);

    // Without units nothing accrues, so the index is set without reading the old one, which
    // may belong to an earlier schedule in another asset.
    if (units == 0)
    {
        mptoken->at(sfCouponIndex) = perUnit;
        return;
    }

    STAmount const zero{perUnit.asset(), 0};
    auto const indexField = mptoken->at(~sfCouponIndex);
    STAmount const index = indexField ? *indexField : zero;

    // Nothing declared since this holder last settled.
    if (perUnit <= index)
        return;

    // A pseudo-account cannot claim, so its share stays in the pool for the payer.
    if (!isPseudoAccount(view, mptoken->at(sfAccount)))
    {
        auto const accruedField = mptoken->at(~sfCouponAccrued);
        STAmount const accrued = accruedField ? *accruedField : zero;

        NumberRoundModeGuard const mg(Number::RoundingMode::Downward);
        Number const addition = Number(units) * (Number(perUnit) - Number(index));
        STAmount const additionAmt{perUnit.asset(), addition};

        // An addition below the precision of the accrued balance rounds down to nothing.
        if (canAdd(accrued, additionAmt))
        {
            bool const wasEmpty = accrued == beast::kZero;
            STAmount const settled = accrued + additionAmt;
            mptoken->at(sfCouponAccrued) = settled;

            // ClaimantCount tracks MPTokens holding an unclaimed balance.
            if (wasEmpty && settled != beast::kZero)
                schedule->at(sfClaimantCount) = schedule->at(sfClaimantCount) + 1;
        }
        else
        {
            JLOG(j.debug()) << "couponSettleMPToken: addition below the accrued precision dropped.";
        }
    }

    mptoken->at(sfCouponIndex) = perUnit;
}

void
couponSettleIfScheduled(ApplyView& view, SLE::ConstRef issuance, SLE::Ref mptoken, beast::Journal j)
{
    if (!issuance || !issuance->isFlag(lsfMPTCouponSchedule))
    {
        // An index left by a deleted schedule must not carry into the next one.
        if (mptoken->isFieldPresent(sfCouponIndex))
            mptoken->makeFieldAbsent(sfCouponIndex);
        return;
    }

    auto const sleSchedule = view.peek(keylet::couponSchedule(mptoken->at(sfMPTokenIssuanceID)));
    if (!sleSchedule)
        return;

    couponSettleMPToken(view, sleSchedule, mptoken, j);
    view.update(sleSchedule);
}

}  // namespace xrpl
