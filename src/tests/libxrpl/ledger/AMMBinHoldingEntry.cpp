#include <xrpl/ledger/entries/AMMBinHoldingEntry.h>

#include <xrpl/protocol/Indexes.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

#include <cstdint>

namespace xrpl::test {

TEST(AMMBinHoldingEntryTests, constructors)
{
    EntryTestEnv e;

    expectKeylet<AMMBinHoldingEntry>(
        e,
        keylet::ammBinHolding(e.someID(), e.alice.id(), std::int32_t{5}),
        "ammBinHolding(ammID, owner, binID)",
        e.someID(),
        e.alice.id(),
        std::int32_t{5});

    expectKeylet<AMMBinHoldingEntry>(
        e, keylet::ammBinHolding(e.someID()), "ammBinHolding(UInt256)", e.someID());
}

}  // namespace xrpl::test
