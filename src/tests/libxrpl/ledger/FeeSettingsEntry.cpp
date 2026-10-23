#include <xrpl/ledger/entries/FeeSettingsEntry.h>

#include <tests/libxrpl/ledger/EntryTestHelpers.h>

#include <xrpl/protocol/Indexes.h>

#include <gtest/gtest.h>

namespace xrpl::test {

TEST(FeeSettingsEntryTests, constructors)
{
    EntryTestEnv e;

    expectKeylet<FeeSettingsEntry>(e, keylet::feeSettings(), "feeSettings()");
}

}  // namespace xrpl::test
