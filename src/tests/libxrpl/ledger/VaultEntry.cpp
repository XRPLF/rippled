#include <xrpl/ledger/entries/VaultEntry.h>

#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SeqProxy.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(VaultEntryTests, constructors)
{
    EntryTestEnv e;

    SeqProxy const seq = SeqProxy::rawSequence(8);

    expectKeylet<VaultEntry>(
        e, keylet::vault(e.alice.id(), seq), "vault(owner, seq)", e.alice.id(), seq);

    expectKeylet<VaultEntry>(e, keylet::vault(e.someID()), "vault(UInt256)", e.someID());
}

}  // namespace xrpl::test
