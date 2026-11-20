#include <xrpl/ledger/entries/DIDEntry.h>

#include <tests/libxrpl/ledger/EntryTestHelpers.h>

#include <xrpl/protocol/Indexes.h>

#include <gtest/gtest.h>

namespace xrpl::test {

TEST(DIDEntryTests, constructors)
{
    EntryTestEnv e;

    expectKeylet<DIDEntry>(e, keylet::did(e.alice.id()), "did(account)", e.alice.id());
}

}  // namespace xrpl::test
