#include <xrpl/ledger/entries/OfferEntry.h>

#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SeqProxy.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(OfferEntryTests, Constructors)
{
    EntryTestEnv e;

    SeqProxy const seq = SeqProxy::rawSequence(3);

    expectKeylet<OfferEntry>(
        e, keylet::offer(e.alice.id(), seq), "offer(id, seq)", e.alice.id(), seq);

    expectKeylet<OfferEntry>(e, keylet::offer(e.someID()), "offer(UInt256)", e.someID());
}

}  // namespace xrpl::test
