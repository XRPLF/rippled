#include <xrpl/ledger/entries/SignerListEntry.h>

#include <tests/libxrpl/ledger/EntryTestHelpers.h>

#include <xrpl/protocol/Indexes.h>

#include <gtest/gtest.h>

namespace xrpl::test {

TEST(SignerListEntryTests, constructors)
{
    EntryTestEnv e;

    expectKeylet<SignerListEntry>(
        e, keylet::signerList(e.alice.id()), "signerList(account)", e.alice.id());
}

}  // namespace xrpl::test
