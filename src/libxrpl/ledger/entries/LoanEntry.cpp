#include <xrpl/ledger/entries/LoanEntry.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>

namespace xrpl {

template <typename ViewT>
LoanState
LoanEntry<ViewT>::state() const
{
    XRPL_ASSERT(
        this->exists() && (*this)->getType() == ltLOAN,
        "xrpl::constructLoanState : valid loan SLE");

    return constructLoanState(
        (*this)->at(sfTotalValueOutstanding),
        (*this)->at(sfPrincipalOutstanding),
        (*this)->at(sfManagementFeeOutstanding));
}

template <typename ViewT>
bool
LoanEntry<ViewT>::isPaymentLate() const
{
    auto const& view = this->readView();
    return hasExpired(
        view,
        (*this)->at(sfNextPaymentDueDate),
        view.rules().enabled(fixCleanup3_4_0) ? ExpiryComparison::Exclusive
                                              : ExpiryComparison::Inclusive);
}

template class LoanEntry<ReadView>;
template class LoanEntry<ApplyView>;

}  // namespace xrpl
