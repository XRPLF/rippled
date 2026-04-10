#include <xrpl/tx/invariants/SponsorshipInvariant.h>
//
#include <xrpl/basics/Log.h>

namespace xrpl {

// Add new sponsorship-related invariants implementations
void
SponsorshipOwnerCountsMatch::visitEntry(
    bool,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    auto getSponsored = [](std::shared_ptr<SLE const> const& sle) -> std::uint32_t {
        if (sle && sle->getType() == ltACCOUNT_ROOT)
            return sle->getFieldU32(sfSponsoredOwnerCount);
        return 0;
    };
    auto getSponsoring = [](std::shared_ptr<SLE const> const& sle) -> std::uint32_t {
        if (sle && sle->getType() == ltACCOUNT_ROOT)
            return sle->getFieldU32(sfSponsoringOwnerCount);
        return 0;
    };

    auto getOwnerCount = [](std::shared_ptr<SLE const> const& sle) -> std::uint32_t {
        if (sle && sle->getType() == ltACCOUNT_ROOT)
            return sle->getFieldU32(sfOwnerCount);
        return 0;
    };
    auto getSponsoredOwnerCount = [](std::shared_ptr<SLE const> const& sle) -> std::uint32_t {
        if (sle && sle->getType() == ltACCOUNT_ROOT)
            return sle->getFieldU32(sfSponsoredOwnerCount);
        return 0;
    };

    std::int64_t const beforeSponsored = getSponsored(before);
    std::int64_t const afterSponsored = getSponsored(after);
    std::int64_t const beforeSponsoring = getSponsoring(before);
    std::int64_t const afterSponsoring = getSponsoring(after);

    deltaSponsoredOwnerCount_ += (afterSponsored - beforeSponsored);
    deltaSponsoringOwnerCount_ += (afterSponsoring - beforeSponsoring);

    if (getOwnerCount(after) < getSponsoredOwnerCount(after))
        invalidOwnerCountLessThanSponsoredOwnerCount_ += 1;
}

bool
SponsorshipOwnerCountsMatch::finalize(
    STTx const&,
    TER const,
    XRPAmount const,
    ReadView const&,
    beast::Journal const& j)
{
    if (deltaSponsoredOwnerCount_ != deltaSponsoringOwnerCount_)
    {
        JLOG(j.fatal()) << "Invariant failed: SponsoredOwnerCount does not "
                           "equal SponsoringOwnerCount delta.";
        return false;
    }

    if (invalidOwnerCountLessThanSponsoredOwnerCount_ > 0)
    {
        JLOG(j.fatal())
            << "Invariant failed: OwnerCount must be greater than or equal to SponsoredOwnerCount.";
        return false;
    }

    return true;
}

void
SponsorshipAccountCountMatchesField::visitEntry(
    bool,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    auto getSponsoringAccountCount = [](std::shared_ptr<SLE const> const& sle) -> std::uint32_t {
        if (sle && sle->getType() == ltACCOUNT_ROOT)
            return sle->getFieldU32(sfSponsoringAccountCount);
        return 0;
    };

    auto hasSponsorField = [](std::shared_ptr<SLE const> const& sle) -> bool {
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
    beast::Journal const& j)
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
