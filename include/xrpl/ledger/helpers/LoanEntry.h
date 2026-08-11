#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SeqProxy.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
class LoanEntry : public SLEBase<ViewT, ltLOAN>
{
public:
    using Base = SLEBase<ViewT, ltLOAN>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit LoanEntry(
        uint256 const& loanBrokerID,
        SeqProxy const& loanSeq,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::loan(loanBrokerID, loanSeq), view, j)
    {
    }

    explicit LoanEntry(
        uint256 const& loanID,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::loan(loanID), view, j)
    {
    }
};

using RLoanEntry = LoanEntry<ReadView>;
using WLoanEntry = LoanEntry<ApplyView>;

}  // namespace xrpl
