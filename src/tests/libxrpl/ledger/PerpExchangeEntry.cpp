#include <xrpl/ledger/entries/PerpExchangeEntry.h>

#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SeqProxy.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(PerpExchangeEntryTests, constructors)
{
    EntryTestEnv e;

    SeqProxy const seq = SeqProxy::rawSequence(8);

    expectKeylet<PerpExchangeEntry>(
        e, keylet::perpExchange(e.alice.id(), seq), "perpExchange(owner, seq)", e.alice.id(), seq);

    expectKeylet<PerpExchangeEntry>(
        e, keylet::perpExchange(e.someID()), "perpExchange(UInt256)", e.someID());
}

}  // namespace xrpl::test
