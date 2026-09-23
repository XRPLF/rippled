#include <xrpl/ledger/entries/FeeSettingsEntry.h>

#include <xrpl/protocol/Indexes.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(FeeSettingsEntryTests, constructors)
{
    EntryTestEnv e;

    expectKeylet<FeeSettingsEntry>(e, keylet::feeSettings(), "feeSettings()");
}

}  // namespace xrpl::test
