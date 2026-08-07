#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
class LoanBrokerEntry : public SLEBase<ViewT, ltLOAN_BROKER>
{
public:
    using Base = SLEBase<ViewT, ltLOAN_BROKER>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit LoanBrokerEntry(
        AccountID const& owner,
        std::uint32_t seq,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::loanBroker(owner, seq), view, j)
    {
    }

    explicit LoanBrokerEntry(
        uint256 const& loanBrokerID,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::loanBroker(loanBrokerID), view, j)
    {
    }
};

using RLoanBrokerEntry = LoanBrokerEntry<ReadView>;
using WLoanBrokerEntry = LoanBrokerEntry<ApplyView>;

}  // namespace xrpl
