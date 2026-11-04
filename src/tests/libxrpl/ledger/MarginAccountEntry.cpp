#include <xrpl/ledger/entries/MarginAccountEntry.h>

#include <xrpl/protocol/Indexes.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(MarginAccountEntryTests, constructors)
{
    EntryTestEnv e;

    expectKeylet<MarginAccountEntry>(
        e,
        keylet::marginAccount(e.alice.id(), e.someID()),
        "marginAccount(account, exchangeID)",
        e.alice.id(),
        e.someID());

    expectKeylet<MarginAccountEntry>(
        e, keylet::marginAccount(e.someID()), "marginAccount(UInt256)", e.someID());
}

}  // namespace xrpl::test
