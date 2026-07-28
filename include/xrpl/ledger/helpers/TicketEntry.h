#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/AccountID.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
class TicketEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit TicketEntry(
        AccountID const& id,
        std::uint32_t ticketSeq,
        SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::ticket(id, ticketSeq), view, j)
    {
    }
};

}  // namespace xrpl
