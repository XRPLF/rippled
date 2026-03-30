#pragma once

#include <xrpl/protocol/XRPAmount.h>

namespace xrpl {

// Deprecated constant for backwards compatibility with pre-XRPFees amendment.
// This was the reference fee units used in the old fee calculation.
inline constexpr std::uint32_t FEE_UNITS_DEPRECATED = 10;

/** Reflects the fee settings for a particular ledger.

    The fees are always the same for any transactions applied
    to a ledger. Changes to fees occur in between ledgers.
*/
struct Fees
{
    /** @brief Cost of a reference transaction in drops. */
    XRPAmount base{0};

    /** @brief Minimum XRP an account must hold to exist on the ledger. */
    XRPAmount reserve{0};

    /** @brief Additional XRP reserve required per owned ledger object. */
    XRPAmount increment{0};

    explicit Fees() = default;
    Fees(Fees const&) = default;
    Fees&
    operator=(Fees const&) = default;

    Fees(XRPAmount base_, XRPAmount reserve_, XRPAmount increment_)
        : base(base_), reserve(reserve_), increment(increment_)
    {
    }

    /** Returns the account reserve given the owner count, in drops.

        The reserve is calculated as the reserve base plus
        the reserve increment times the number of increments.
    */
    XRPAmount
    accountReserve(
        std::size_t ownerCount,
        std::size_t sponsoredOwnerCount = 0,
        std::size_t sponsoringOwnerCount = 0,
        bool isAccountSponsored = false,
        std::size_t sponsoringAccountCount = 0) const
    {
        auto const accountReserveUnits = (isAccountSponsored ? 0 : 1) + sponsoringAccountCount;

        XRPL_ASSERT(
            ownerCount >= sponsoredOwnerCount,
            "xrpl::Fees::accountReserve : OwnerCount must be greater than or equal to "
            "SponsoredOwnerCount");

        auto const ownerReserveUnits = (ownerCount - sponsoredOwnerCount) + sponsoringOwnerCount;

        return (reserve * accountReserveUnits) + (increment * ownerReserveUnits);
    }
};

}  // namespace xrpl
