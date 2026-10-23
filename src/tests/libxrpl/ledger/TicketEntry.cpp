#include <xrpl/ledger/entries/TicketEntry.h>

#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SeqProxy.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(TicketEntryTests, constructors)
{
    EntryTestEnv e;

    SeqProxy const ticketSeq = SeqProxy::rawTicket(2);

    expectKeylet<TicketEntry>(
        e,
        keylet::ticket(e.alice.id(), ticketSeq),
        "ticket(id, ticketSeq)",
        e.alice.id(),
        ticketSeq);

    expectKeylet<TicketEntry>(e, keylet::ticket(e.someID()), "ticket(UInt256)", e.someID());
}

}  // namespace xrpl::test
