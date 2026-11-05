#include <xrpl/ledger/entries/PasskeyListEntry.h>

#include <xrpl/protocol/Indexes.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(PasskeyListEntryTests, constructors)
{
    EntryTestEnv e;

    expectKeylet<PasskeyListEntry>(
        e, keylet::passkeyList(e.alice.id()), "passkeyList(account)", e.alice.id());
}

}  // namespace xrpl::test
