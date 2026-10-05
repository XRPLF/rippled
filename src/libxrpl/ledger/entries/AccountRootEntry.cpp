#include <xrpl/ledger/entries/AccountRootEntry.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>
#include <limits>

namespace xrpl {

template <typename ViewT>
XRPAmount
AccountRootEntry<ViewT>::reserve(Adjustment adj) const
{
    auto const& view = this->readView();
    auto const j = this->journal();

    XRPL_ASSERT(
        this->exists() && (*this)->getType() == ltACCOUNT_ROOT, "xrpl::accountReserve : valid sle");

    if (!view.rules().enabled(featureSponsor))
    {
        XRPL_ASSERT(adj.accountCountDelta == 0, "xrpl::accountReserve : no account count delta");
        return view.fees().accountReserve(
            (*this)->getFieldU32(sfOwnerCount) + adj.ownerCountDelta, 1);
    }
    std::uint32_t const currentOwnerCount = ownerCount(adj.ownerCountDelta);
    std::uint32_t const currentAccountCount =
        detail::accountCountImpl(*this, adj.accountCountDelta, j);

    return view.fees().accountReserve(currentOwnerCount, currentAccountCount);
}

template <typename ViewT>
std::uint32_t
AccountRootEntry<ViewT>::ownerCount(std::int32_t ownerCountAdj) const
{
    auto const j = this->journal();

    XRPL_ASSERT(
        this->exists() && (*this)->getType() == ltACCOUNT_ROOT,
        "xrpl::ownerCount : sle is account root");

    AccountID const id = (*this)->getAccountID(sfAccount);
    std::uint32_t const currentOwnerCount = (*this)->at(sfOwnerCount);
    std::uint32_t const sponsoredOwnerCount = (*this)->at(sfSponsoredOwnerCount);
    std::uint32_t const sponsoringOwnerCount = (*this)->at(sfSponsoringOwnerCount);

    XRPL_ASSERT(
        currentOwnerCount >= sponsoredOwnerCount,
        "xrpl::ownerCount : OwnerCount must be greater than or equal to SponsoredOwnerCount");

    std::int64_t deltaCount =
        static_cast<std::int64_t>(ownerCountAdj) - sponsoredOwnerCount + sponsoringOwnerCount;

    if (deltaCount > std::numeric_limits<std::int32_t>::max())
    {
        // LCOV_EXCL_START
        deltaCount = std::numeric_limits<std::int32_t>::max();
        JLOG(j.fatal()) << "Account " << id << " delta count exceeds max, "
                        << "adjustment: " << ownerCountAdj
                        << ", sponsoredCount: " << sponsoredOwnerCount
                        << ", sponsoringOwnerCount: " << sponsoringOwnerCount;
        // LCOV_EXCL_STOP
    }
    else if (deltaCount < std::numeric_limits<std::int32_t>::min())
    {
        // LCOV_EXCL_START
        deltaCount = std::numeric_limits<std::int32_t>::min();
        JLOG(j.fatal()) << "Account " << id << " delta count is below min, "
                        << "adjustment: " << ownerCountAdj
                        << ", sponsoredCount: " << sponsoredOwnerCount
                        << ", sponsoringCount: " << sponsoringOwnerCount;
        // LCOV_EXCL_STOP
    }

    return detail::confineOwnerCount(currentOwnerCount, deltaCount);
}

template class AccountRootEntry<ReadView>;
template class AccountRootEntry<ApplyView>;

}  // namespace xrpl
