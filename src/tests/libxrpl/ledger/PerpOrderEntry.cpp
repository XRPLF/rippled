#include <xrpl/ledger/entries/PerpOrderEntry.h>

#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SeqProxy.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(PerpOrderEntryTests, constructors)
{
    EntryTestEnv e;

    SeqProxy const seq = SeqProxy::rawSequence(9);

    expectKeylet<PerpOrderEntry>(
        e, keylet::perpOrder(e.alice.id(), seq), "perpOrder(account, seq)", e.alice.id(), seq);

    expectKeylet<PerpOrderEntry>(
        e, keylet::perpOrder(e.someID()), "perpOrder(UInt256)", e.someID());
}

}  // namespace xrpl::test
