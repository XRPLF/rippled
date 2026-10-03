#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SeqProxy.h>

namespace xrpl {

// Defined in <xrpl/ledger/helpers/LendingHelpers.h>, which includes this
// header; include that header to call state().
struct LoanState;

template <typename ViewT>
class LoanEntry : public SLEBase<ViewT, ltLOAN>
{
public:
    using Base = SLEBase<ViewT, ltLOAN>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit LoanEntry(
        UInt256 const& loanBrokerID,
        SeqProxy const& loanSeq,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::loan(loanBrokerID, loanSeq), view, j)
    {
    }

    explicit LoanEntry(
        UInt256 const& loanID,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::loan(loanID), view, j)
    {
    }

    // Build a LoanState from the three tracked fields. The Loan ledger object
    // always holds rounded values.
    [[nodiscard]] LoanState
    state() const;

    // Returns true if the loan's next payment is late per protocol rules. The
    // boundary is amendment-gated: with fixCleanup3_4_0 the due date must be
    // strictly in the past, otherwise the exact due-date instant counts as late.
    [[nodiscard]] bool
    isPaymentLate() const;

    // Instant interest recognition (pre-LendingProtocolV1_1): the vault's
    // exposure to this loan, used by LoanManage impair/unimpair/default.
    //
    // XLS-66 section 3.2.3.2, defines the default amount as
    //
    // DefaultAmount = (Loan.PrincipalOutstanding + Loan.InterestOutstanding)
    //
    // Which is equivalent to (Loan.TotalValueOutstanding - Loan.ManagementFeeOutstanding)
    [[nodiscard]] Number
    vaultExposureInstantRecognition() const
    {
        return (*this)->at(sfTotalValueOutstanding) - (*this)->at(sfManagementFeeOutstanding);
    }

    // Cash-basis (LendingProtocolV1_1) recognition model: the vault's exposure
    // to this loan is its principal only.
    //
    // DefaultAmount = Loan.PrincipalOutstanding
    [[nodiscard]] Number
    vaultExposureCashBasis() const
    {
        return (*this)->at(sfPrincipalOutstanding);
    }
};

using LoanEntryR = LoanEntry<ReadView>;
using LoanEntryW = LoanEntry<ApplyView>;

}  // namespace xrpl
