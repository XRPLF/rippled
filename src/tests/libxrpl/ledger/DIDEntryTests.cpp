#include <xrpl/ledger/helpers/DIDEntry.h>
#include <xrpl/protocol/Indexes.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(DIDEntryTests, Constructors)
{
    EntryTestEnv e;

    expectKeylet<DIDEntry>(e, keylet::did(e.alice.id()), "did(account)", e.alice.id());
}

}  // namespace xrpl::test
