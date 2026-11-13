#include <xrpl/ledger/entries/NegativeUNLEntry.h>

#include <tests/libxrpl/ledger/EntryTestHelpers.h>

#include <xrpl/protocol/Indexes.h>

#include <gtest/gtest.h>

namespace xrpl::test {

TEST(NegativeUNLEntryTests, constructors)
{
    EntryTestEnv e;

    expectKeylet<NegativeUNLEntry>(e, keylet::negativeUNL(), "negativeUNL()");
}

}  // namespace xrpl::test
