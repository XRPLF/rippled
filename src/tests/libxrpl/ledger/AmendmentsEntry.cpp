#include <xrpl/ledger/entries/AmendmentsEntry.h>

#include <xrpl/protocol/Indexes.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(AmendmentsEntryTests, constructors)
{
    EntryTestEnv e;

    expectKeylet<AmendmentsEntry>(e, keylet::amendments(), "amendments()");
}

}  // namespace xrpl::test
