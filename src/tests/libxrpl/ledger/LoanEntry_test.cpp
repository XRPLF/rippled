#include <xrpl/ledger/helpers/LoanEntry.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SeqProxy.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(LoanEntryTests, Constructors)
{
    EntryTestEnv e;

    SeqProxy const seq = SeqProxy::rawSequence(9);

    expectKeylet<LoanEntry>(
        e, keylet::loan(e.someID(), seq), "loan(loanBrokerID, loanSeq)", e.someID(), seq);

    expectKeylet<LoanEntry>(e, keylet::loan(e.someID()), "loan(uint256)", e.someID());

    // Both overloads start with the same uint256, so they must not produce
    // the same key -- otherwise arity is the only thing keeping them apart
    // and the test proves nothing.
    EXPECT_NE(keylet::loan(e.someID(), seq).key, keylet::loan(e.someID()).key);
}

}  // namespace xrpl::test
