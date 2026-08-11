#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SeqProxy.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
class TicketEntry : public SLEBase<ViewT, ltTICKET>
{
public:
    using Base = SLEBase<ViewT, ltTICKET>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit TicketEntry(
        AccountID const& id,
        SeqProxy const& ticketSeq,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::ticket(id, ticketSeq), view, j)
    {
    }

    explicit TicketEntry(
        uint256 const& ticketID,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::ticket(ticketID), view, j)
    {
    }
};

using RTicketEntry = TicketEntry<ReadView>;
using WTicketEntry = TicketEntry<ApplyView>;

}  // namespace xrpl
