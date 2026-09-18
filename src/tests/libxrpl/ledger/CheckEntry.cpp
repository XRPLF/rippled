#include <xrpl/ledger/helpers/CheckEntry.h>

#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SeqProxy.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(CheckEntryTests, Constructors)
{
    EntryTestEnv e;

    SeqProxy const seq = SeqProxy::rawSequence(7);

    expectKeylet<CheckEntry>(
        e, keylet::check(e.alice.id(), seq), "check(id, seq)", e.alice.id(), seq);

    expectKeylet<CheckEntry>(e, keylet::check(e.someID()), "check(uint256)", e.someID());
}

}  // namespace xrpl::test
