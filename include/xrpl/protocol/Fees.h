#ifndef XRPL_PROTOCOL_FEES_H_INCLUDED
#define XRPL_PROTOCOL_FEES_H_INCLUDED

#include <xrpl/protocol/XRPAmount.h>

namespace xrpl {

/** Reflects the fee settings for a particular ledger.

    The fees are always the same for any transactions applied
    to a ledger. Changes to fees occur in between ledgers.
*/
struct Fees
{
    XRPAmount base{0};       // Reference tx cost (drops)
    XRPAmount reserve{0};    // Reserve base (drops)
    XRPAmount increment{0};  // Reserve increment (drops)

    explicit Fees() = default;
    Fees(Fees const&) = default;
    Fees&
    operator=(Fees const&) = default;

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
        return (isAccountSponsored ? XRPAmount(0) : reserve) +
            increment *
            (ownerCount + sponsoringOwnerCount - sponsoredOwnerCount) +
            reserve * sponsoringAccountCount;
    }
};

}  // namespace xrpl

#endif
