#include <xrpl/ledger/entries/AMMEntry.h>

#include <tests/libxrpl/helpers/IOU.h>
#include <tests/libxrpl/ledger/EntryTestHelpers.h>

#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>

#include <gtest/gtest.h>

namespace xrpl::test {

TEST(AMMEntryTests, constructors)
{
    EntryTestEnv e;

    Asset const xrp{xrpIssue()};
    Asset const usd{IOU("USD", e.alice).issue()};

    expectKeylet<AMMEntry>(e, keylet::amm(xrp, usd), "amm(asset, asset)", xrp, usd);

    expectKeylet<AMMEntry>(e, keylet::amm(e.someID()), "amm(UInt256)", e.someID());
}

}  // namespace xrpl::test
