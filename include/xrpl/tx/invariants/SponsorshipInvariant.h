#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>

namespace xrpl {

/**
 * @brief Invariant: Sponsored owner counts are balanced.
 *
 * The following check is made for every transaction:
 *  - The sum of all per-account deltas of `sfSponsoredOwnerCount` equals
 *    the sum of all per-account deltas of `sfSponsoringOwnerCount`.
 *  - Account OwnerCount must be greater than or equal to SponsoredOwnerCount.
 */
class SponsorshipOwnerCountsMatch
{
    std::int64_t deltaSponsoredOwnerCount_ = 0;
    std::int64_t deltaSponsoringOwnerCount_ = 0;
    std::uint64_t invalidOwnerCountLessThanSponsoredOwnerCount_ = 0;

public:
    void
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

/**
 * @brief Invariant: Sponsoring account relationships tracked consistently.
 *
 * The following check is made for every transaction:
 *  - The net delta of `sfSponsoringAccountCount` across all accounts equals
 *    the net delta of the count of ltACCOUNT_ROOT entries having
 *    `sfSponsor` present (presence transitions only: add/remove).
 */
class SponsorshipAccountCountMatchesField
{
    std::int64_t deltaSponsoringAccountCount_ = 0;
    std::int64_t deltaSponsorFieldPresence_ = 0;

public:
    void
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

}  // namespace xrpl
