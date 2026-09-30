#include <xrpl/ledger/entries/AccountRootEntry.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>

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
    std::uint32_t const currentOwnerCount = ownerCount(*this, j, adj.ownerCountDelta);
    std::uint32_t const currentAccountCount =
        detail::accountCountImpl(*this, adj.accountCountDelta, j);

    return view.fees().accountReserve(currentOwnerCount, currentAccountCount);
}

template class AccountRootEntry<ReadView>;
template class AccountRootEntry<ApplyView>;

}  // namespace xrpl
