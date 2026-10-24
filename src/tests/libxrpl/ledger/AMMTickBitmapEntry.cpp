#include <xrpl/ledger/entries/AMMTickBitmapEntry.h>

#include <xrpl/protocol/Indexes.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

#include <cstdint>

namespace xrpl::test {

TEST(AMMTickBitmapEntryTests, constructors)
{
    EntryTestEnv e;

    expectKeylet<AMMTickBitmapEntry>(
        e,
        keylet::ammTickBitmapWord(e.someID(), std::uint16_t{12}),
        "ammTickBitmapWord(ammID, wordIndex)",
        e.someID(),
        std::uint16_t{12});

    expectKeylet<AMMTickBitmapEntry>(
        e, keylet::ammTickBitmapWord(e.someID()), "ammTickBitmapWord(UInt256)", e.someID());
}

}  // namespace xrpl::test
