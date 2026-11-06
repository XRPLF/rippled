#include <xrpl/ledger/entries/EscrowEntry.h>

#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SeqProxy.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(EscrowEntryTests, constructors)
{
    EntryTestEnv e;

    SeqProxy const seq = SeqProxy::rawSequence(11);

    expectKeylet<EscrowEntry>(
        e, keylet::escrow(e.alice.id(), seq), "escrow(src, seq)", e.alice.id(), seq);
}

}  // namespace xrpl::test
