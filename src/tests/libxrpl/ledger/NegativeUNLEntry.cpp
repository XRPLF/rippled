#include <xrpl/ledger/entries/NegativeUNLEntry.h>

#include <xrpl/protocol/Indexes.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(NegativeUNLEntryTests, Constructors)
{
    EntryTestEnv e;

    expectKeylet<NegativeUNLEntry>(e, keylet::negativeUNL(), "negativeUNL()");
}

}  // namespace xrpl::test
