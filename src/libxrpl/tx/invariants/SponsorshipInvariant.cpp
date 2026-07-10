#include <xrpl/tx/invariants/SponsorshipInvariant.h>
//
#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>

namespace xrpl {

// Add new sponsorship-related invariants implementations
void
SponsorshipOwnerCountsMatch::visitEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after)
{
    auto getSponsored = [](SLE::const_ref sle) -> std::uint32_t {
        if (sle && sle->getType() == ltACCOUNT_ROOT)
            return sle->getFieldU32(sfSponsoredOwnerCount);
        return 0;
    };
    auto getSponsoring = [](SLE::const_ref sle) -> std::uint32_t {
        if (sle && sle->getType() == ltACCOUNT_ROOT)
            return sle->getFieldU32(sfSponsoringOwnerCount);
        return 0;
    };

    auto getOwnerCount = [](SLE::const_ref sle) -> std::uint32_t {
        if (sle && sle->getType() == ltACCOUNT_ROOT)
            return sle->getFieldU32(sfOwnerCount);
        return 0;
    };

    auto getSponsoredObjectOwnerCount = [&](SLE::const_ref sle) -> std::uint32_t {
        if (!sle)
            return 0;
        switch (sle->getType())
        {
            case ltACCOUNT_ROOT:
                return 0;
            case ltRIPPLE_STATE: {
                // A trust line can be reserve-sponsored independently on each
                // side, so it may contribute up to two sponsored owner counts.
                uint32_t ownerCount = 0;
                if (sle->isFieldPresent(sfHighSponsor))
                    ownerCount++;
                if (sle->isFieldPresent(sfLowSponsor))
                    ownerCount++;
                return ownerCount;
            }
            default:
                // Every other supported type carries a single sfSponsor field
                // and contributes its full owner-count magnitude only when it is
                // sponsored.
                if (!sle->isFieldPresent(sfSponsor))
                    return 0;
                return getLedgerEntryOwnerCount(*sle);
        }
    };

    // The values are implicitly casted to std::int64_t to calculate deltas.
    std::int64_t const beforeSponsored = getSponsored(before);
    std::int64_t const afterSponsored = getSponsored(after);
    std::int64_t const beforeSponsoring = getSponsoring(before);
    std::int64_t const afterSponsoring = getSponsoring(after);

    std::int64_t const beforeSponsoredObjectOwnerCount = getSponsoredObjectOwnerCount(before);
    std::int64_t const afterSponsoredObjectOwnerCount =
        isDelete ? 0 : getSponsoredObjectOwnerCount(after);

    deltaSponsoredOwnerCount_ += (afterSponsored - beforeSponsored);
    deltaSponsoringOwnerCount_ += (afterSponsoring - beforeSponsoring);

    deltaSponsoredObjectOwnerCount_ +=
        (afterSponsoredObjectOwnerCount - beforeSponsoredObjectOwnerCount);

    if (getOwnerCount(after) < getSponsored(after))
        ownerCountBelowSponsored_ += 1;
}

bool
SponsorshipOwnerCountsMatch::finalize(
    STTx const&,
    TER const,
    XRPAmount const,
    ReadView const&,
    beast::Journal const& j) const
{
    if (deltaSponsoredOwnerCount_ != deltaSponsoringOwnerCount_)
    {
        JLOG(j.fatal()) << "Invariant failed: SponsoredOwnerCount does not "
                           "equal SponsoringOwnerCount delta.";
        return false;
    }

    if (ownerCountBelowSponsored_ > 0)
    {
        JLOG(j.fatal())
            << "Invariant failed: OwnerCount must be greater than or equal to SponsoredOwnerCount.";
        return false;
    }

    if (deltaSponsoredObjectOwnerCount_ != deltaSponsoredOwnerCount_)
    {
        JLOG(j.fatal()) << "Invariant failed: SponsoredObjectOwnerCount does not "
                           "equal SponsoredOwnerCount delta.";
        return false;
    }

    return true;
}

void
SponsorshipAccountCountMatchesField::visitEntry(bool, SLE::const_ref before, SLE::const_ref after)
{
    auto getSponsoringAccountCount = [](SLE::const_ref sle) -> std::uint32_t {
        if (sle && sle->getType() == ltACCOUNT_ROOT)
            return sle->getFieldU32(sfSponsoringAccountCount);
        return 0;
    };

    auto hasSponsorField = [](SLE::const_ref sle) -> bool {
        return sle && sle->getType() == ltACCOUNT_ROOT && sle->isFieldPresent(sfSponsor);
    };

    std::int64_t const beforeCount = getSponsoringAccountCount(before);
    std::int64_t const afterCount = getSponsoringAccountCount(after);
    deltaSponsoringAccountCount_ += (afterCount - beforeCount);

    int const beforePresent = hasSponsorField(before) ? 1 : 0;
    int const afterPresent = hasSponsorField(after) ? 1 : 0;
    deltaSponsorFieldPresence_ += (afterPresent - beforePresent);
}

bool
SponsorshipAccountCountMatchesField::finalize(
    STTx const&,
    TER const,
    XRPAmount const,
    ReadView const&,
    beast::Journal const& j) const
{
    if (deltaSponsoringAccountCount_ != deltaSponsorFieldPresence_)
    {
        JLOG(j.fatal()) << "Invariant failed: Net delta of SponsoringAccountCount does not "
                           "match net delta of sfSponsor presence.";
        return false;
    }

    return true;
}

}  // namespace xrpl
