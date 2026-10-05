#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>
#include <optional>

namespace xrpl {

template <typename ViewT>
class SponsorshipEntry : public SLEBase<ViewT, ltSPONSORSHIP>
{
public:
    using Base = SLEBase<ViewT, ltSPONSORSHIP>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit SponsorshipEntry(
        AccountID const& sponsor,
        AccountID const& sponsee,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::sponsorship(sponsor, sponsee), view, j)
    {
    }

    /**
     * Computes the resulting RemainingOwnerCount using signed 64-bit
     * arithmetic to avoid unsigned wraparound.
     *
     * A missing entry (object creation) or absent field counts as zero.
     * Callers handle the out-of-range results: a negative value is clamped to
     * zero (field absent) and overflow is rejected in preclaim.
     *
     * @param remainingOwnerCountDelta The requested change, if any
     * @return The current RemainingOwnerCount plus the delta
     */
    [[nodiscard]] std::int64_t
    totalRemainingOwnerCount(std::optional<std::int32_t> const& remainingOwnerCountDelta) const
    {
        std::uint32_t const currentCount =
            this->exists() ? (**this)[~sfRemainingOwnerCount].value_or(0u) : 0u;
        return static_cast<std::int64_t>(currentCount) + remainingOwnerCountDelta.value_or(0);
    }

    /**
     * Returns true if applying the deltas leaves the Sponsorship with a
     * budget: a positive FeeAmount or a positive RemainingOwnerCount.
     *
     * A missing entry (object creation) requires both deltas, when present,
     * to be positive.
     *
     * @param feeAmountDelta The requested FeeAmount change, if any
     * @param remainingOwnerCountDelta The requested RemainingOwnerCount change, if any
     * @return True if the resulting FeeAmount or RemainingOwnerCount would be
     *     positive; false otherwise
     */
    [[nodiscard]] bool
    hasBudget(
        std::optional<STAmount> const& feeAmountDelta,
        std::optional<std::int32_t> const& remainingOwnerCountDelta) const;

    /**
     * Consumes the sponsor's pre-funded reserve budget: lowers
     * RemainingOwnerCount by delta and updates the entry.
     *
     * @param delta The number of owner reserves to consume
     * @return tesSUCCESS, or tefINTERNAL if the count is too low
     */
    [[nodiscard]] TER
    decrementPrefundedReserveCount(std::uint32_t delta)
        requires Base::kIsWritable;

    /**
     * Removes the Sponsorship from the sponsor's and sponsee's directories,
     * decrements the sponsor's owner count, returns any pre-funded FeeAmount
     * to the sponsor and erases the Sponsorship from the ledger.
     *
     * The sponsor and sponsee are read from sfOwner and sfSponsee.
     *
     * @return tesSUCCESS, or tecINTERNAL / tefBAD_LEDGER on failure
     */
    [[nodiscard]] TER
    removeFromLedger()
        requires Base::kIsWritable;
};

using SponsorshipEntryR = SponsorshipEntry<ReadView>;
using SponsorshipEntryW = SponsorshipEntry<ApplyView>;

}  // namespace xrpl
