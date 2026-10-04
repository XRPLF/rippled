#include <xrpl/ledger/entries/LoanBrokerEntry.h>

#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SeqProxy.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(LoanBrokerEntryTests, constructors)
{
    EntryTestEnv e;

    SeqProxy const seq = SeqProxy::rawSequence(5);

    expectKeylet<LoanBrokerEntry>(
        e, keylet::loanBroker(e.alice.id(), seq), "loanBroker(owner, seq)", e.alice.id(), seq);

    expectKeylet<LoanBrokerEntry>(
        e, keylet::loanBroker(e.someID()), "loanBroker(UInt256)", e.someID());
}

}  // namespace xrpl::test
