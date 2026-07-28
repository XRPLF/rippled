#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/helpers/SLEBase.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
class LoanEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit LoanEntry(
        uint256 const& loanBrokerID,
        std::uint32_t loanSeq,
        SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::loan(loanBrokerID, loanSeq), view, j)
    {
    }
};

}  // namespace xrpl
