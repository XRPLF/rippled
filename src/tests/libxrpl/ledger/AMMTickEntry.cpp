#include <xrpl/ledger/entries/AMMTickEntry.h>

#include <xrpl/protocol/Indexes.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

#include <cstdint>

namespace xrpl::test {

TEST(AMMTickEntryTests, constructors)
{
    EntryTestEnv e;

    expectKeylet<AMMTickEntry>(
        e,
        keylet::ammTick(e.someID(), std::int32_t{-60}),
        "ammTick(ammID, tickIndex)",
        e.someID(),
        std::int32_t{-60});

    expectKeylet<AMMTickEntry>(e, keylet::ammTick(e.someID()), "ammTick(UInt256)", e.someID());
}

}  // namespace xrpl::test
