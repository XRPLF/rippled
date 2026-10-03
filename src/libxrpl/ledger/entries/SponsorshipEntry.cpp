#include <xrpl/ledger/entries/SponsorshipEntry.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>
#include <optional>

namespace xrpl {

template <typename ViewT>
bool
SponsorshipEntry<ViewT>::hasBudget(
    std::optional<STAmount> const& feeAmountDelta,
    std::optional<std::int32_t> const& remainingOwnerCountDelta) const
{
    // sfFeeAmountDelta and sfRemainingOwnerCountDelta must be non-negative when creating a new
    // Sponsorship object.
    if (!this->exists())
    {
        if (feeAmountDelta.has_value() && *feeAmountDelta <= beast::kZero)
            return false;

        if (remainingOwnerCountDelta.has_value() && *remainingOwnerCountDelta <= 0)
            return false;
    }
    // If the transaction omits a field, it keeps whatever the existing object holds,
    // so fall back to the current SLE value when the tx does not set it.
    STAmount const currentFee =
        this->exists() ? (**this)[~sfFeeAmount].value_or(STAmount{0}) : STAmount{0};
    STAmount const newFee = currentFee + feeAmountDelta.value_or(STAmount{0});

    std::int64_t const newCount = totalRemainingOwnerCount(remainingOwnerCountDelta);

    return newFee > beast::kZero || newCount > 0;
}

template <typename ViewT>
TER
SponsorshipEntry<ViewT>::decrementPrefundedReserveCount(std::uint32_t const delta)
    requires Base::kIsWritable
{
    if (delta == 0)
        return tesSUCCESS;  // LCOV_EXCL_LINE

    auto const currentReserveCount = (*this)->getFieldU32(sfRemainingOwnerCount);
    if (currentReserveCount < delta)
    {
        // LCOV_EXCL_START
        // Already verified by checkReserve (sufficient RemainingOwnerCount)
        UNREACHABLE("xrpl::decrementPrefundedReserveCount : invalid reserve count");
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    (*this)->at(sfRemainingOwnerCount) = currentReserveCount - delta;
    this->update();
    return tesSUCCESS;
}

template <typename ViewT>
TER
SponsorshipEntry<ViewT>::removeFromLedger()
    requires Base::kIsWritable
{
    if (!this->exists())
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto& view = this->applyView();
    auto const j = this->journal();

    auto const sponsorID = (**this)[sfOwner];
    auto const sponseeID = (**this)[sfSponsee];

    // The sponsor owns the Sponsorship object, so deletion releases the
    // sponsor's owner reserve.
    auto sponsorAccSle = view.peek(keylet::account(sponsorID));
    if (!sponsorAccSle)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (!view.dirRemove(keylet::ownerDir(sponsorID), (**this)[sfOwnerNode], this->key(), false))
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "Unable to delete Sponsorship from sponsor.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }
    if (!view.dirRemove(keylet::ownerDir(sponseeID), (**this)[sfSponseeNode], this->key(), false))
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "Unable to delete Sponsorship from sponsee.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    decreaseOwnerCountForObject(view, sponsorAccSle, this->mutableRawSle(), 1, j);

    // Return any prefunded fee amount to the sponsor before erasing the object.
    if ((*this)->isFieldPresent(sfFeeAmount))
    {
        (*sponsorAccSle)[sfBalance] += (*this)->getFieldAmount(sfFeeAmount);
        view.update(sponsorAccSle);
    }

    this->erase();

    return tesSUCCESS;
}

template class SponsorshipEntry<ReadView>;
template class SponsorshipEntry<ApplyView>;

}  // namespace xrpl
