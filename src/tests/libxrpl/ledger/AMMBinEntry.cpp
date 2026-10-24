#include <xrpl/ledger/entries/AMMBinEntry.h>

#include <xrpl/protocol/Indexes.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

#include <cstdint>

namespace xrpl::test {

TEST(AMMBinEntryTests, constructors)
{
    EntryTestEnv e;

    expectKeylet<AMMBinEntry>(
        e,
        keylet::ammBin(e.someID(), std::int32_t{-3}),
        "ammBin(ammID, binID)",
        e.someID(),
        std::int32_t{-3});

    expectKeylet<AMMBinEntry>(e, keylet::ammBin(e.someID()), "ammBin(UInt256)", e.someID());
}

}  // namespace xrpl::test
