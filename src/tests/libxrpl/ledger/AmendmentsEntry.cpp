#include <xrpl/ledger/entries/AmendmentsEntry.h>

#include <tests/libxrpl/ledger/EntryTestHelpers.h>

#include <xrpl/protocol/Indexes.h>

#include <gtest/gtest.h>

namespace xrpl::test {

TEST(AmendmentsEntryTests, constructors)
{
    EntryTestEnv e;

    expectKeylet<AmendmentsEntry>(e, keylet::amendments(), "amendments()");
}

}  // namespace xrpl::test
