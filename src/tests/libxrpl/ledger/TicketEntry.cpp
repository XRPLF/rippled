#include <xrpl/ledger/helpers/TicketEntry.h>

#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SeqProxy.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(TicketEntryTests, Constructors)
{
    EntryTestEnv e;

    SeqProxy const ticketSeq = SeqProxy::rawTicket(2);

    expectKeylet<TicketEntry>(
        e,
        keylet::ticket(e.alice.id(), ticketSeq),
        "ticket(id, ticketSeq)",
        e.alice.id(),
        ticketSeq);

    expectKeylet<TicketEntry>(e, keylet::ticket(e.someID()), "ticket(uint256)", e.someID());
}

}  // namespace xrpl::test
