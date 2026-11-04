#include <xrpl/ledger/entries/MarginAccountEntry.h>

#include <xrpl/protocol/Indexes.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

#include <cstdint>

namespace xrpl::test {

TEST(MarginAccountEntryTests, constructors)
{
    EntryTestEnv e;

    expectKeylet<MarginAccountEntry>(
        e,
        keylet::marginAccount(e.alice.id(), e.someID(), 3u),
        "marginAccount(account, exchangeID, subaccount)",
        e.alice.id(),
        e.someID(),
        std::uint32_t{3});

    expectKeylet<MarginAccountEntry>(
        e, keylet::marginAccount(e.someID()), "marginAccount(UInt256)", e.someID());
}

}  // namespace xrpl::test
