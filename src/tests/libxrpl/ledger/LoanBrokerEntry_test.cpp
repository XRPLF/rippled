#include <xrpl/ledger/helpers/LoanBrokerEntry.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SeqProxy.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(LoanBrokerEntryTests, Constructors)
{
    EntryTestEnv e;

    SeqProxy const seq = SeqProxy::rawSequence(5);

    expectKeylet<LoanBrokerEntry>(
        e, keylet::loanBroker(e.alice.id(), seq), "loanBroker(owner, seq)", e.alice.id(), seq);

    expectKeylet<LoanBrokerEntry>(
        e, keylet::loanBroker(e.someID()), "loanBroker(uint256)", e.someID());
}

}  // namespace xrpl::test
