#include <xrpl/ledger/entries/LedgerHashesEntry.h>

#include <tests/libxrpl/ledger/EntryTestHelpers.h>

#include <xrpl/protocol/Indexes.h>

#include <gtest/gtest.h>

namespace xrpl::test {

TEST(LedgerHashesEntryTests, constructors)
{
    EntryTestEnv e;

    expectKeylet<LedgerHashesEntry>(e, keylet::skip(), "skip()");
}

}  // namespace xrpl::test
