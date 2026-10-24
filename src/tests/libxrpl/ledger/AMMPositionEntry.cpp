#include <xrpl/ledger/entries/AMMPositionEntry.h>

#include <xrpl/protocol/Indexes.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

#include <cstdint>

namespace xrpl::test {

TEST(AMMPositionEntryTests, constructors)
{
    EntryTestEnv e;

    expectKeylet<AMMPositionEntry>(
        e,
        keylet::ammPosition(e.someID(), e.alice.id(), std::uint32_t{7}),
        "ammPosition(ammID, owner, seq)",
        e.someID(),
        e.alice.id(),
        std::uint32_t{7});

    expectKeylet<AMMPositionEntry>(
        e, keylet::ammPosition(e.someID()), "ammPosition(UInt256)", e.someID());
}

}  // namespace xrpl::test
