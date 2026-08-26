#include <xrpl/ledger/helpers/FeeSettingsEntry.h>
#include <xrpl/protocol/Indexes.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(FeeSettingsEntryTests, Constructors)
{
    EntryTestEnv e;

    expectKeylet<FeeSettingsEntry>(e, keylet::feeSettings(), "feeSettings()");
}

}  // namespace xrpl::test
