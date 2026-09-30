#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>

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
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::ticket(id, ticketSeq), view, j)
    {
    }

    explicit TicketEntry(
        UInt256 const& ticketID,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::ticket(ticketID), view, j)
    {
    }

    /**
     * Removes this Ticket from the ledger.
     *
     * Deletes the Ticket, adjusts the account root's TicketCount, and
     * reduces the owner's reserve. @p owner must be the Ticket's owner.
     *
     * @throws std::logic_error if exists() is false (via the underlying
     *         erase()/mutation calls).
     */
    TER
    removeFromLedger(AccountID const& owner)
        requires Base::kIsWritable;
};

using TicketEntryR = TicketEntry<ReadView>;
using TicketEntryW = TicketEntry<ApplyView>;

}  // namespace xrpl
