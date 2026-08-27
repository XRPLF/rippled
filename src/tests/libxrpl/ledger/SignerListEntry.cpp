#include <xrpl/ledger/helpers/SignerListEntry.h>

#include <xrpl/protocol/Indexes.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(SignerListEntryTests, Constructors)
{
    EntryTestEnv e;

    expectKeylet<SignerListEntry>(
        e, keylet::signerList(e.alice.id()), "signerList(account)", e.alice.id());
}

}  // namespace xrpl::test
