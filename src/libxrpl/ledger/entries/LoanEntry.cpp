#include <xrpl/ledger/entries/LoanEntry.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>

namespace xrpl {

template <typename ViewT>
LoanState
LoanEntry<ViewT>::state() const
{
    XRPL_ASSERT(
        this->exists() && this->sleRef().getType() == ltLOAN,
        "xrpl::constructLoanState : valid loan SLE");

    return constructLoanState(
        this->sleRef().at(sfTotalValueOutstanding),
        this->sleRef().at(sfPrincipalOutstanding),
        this->sleRef().at(sfManagementFeeOutstanding));
}

template class LoanEntry<ReadView>;
template class LoanEntry<ApplyView>;

}  // namespace xrpl
