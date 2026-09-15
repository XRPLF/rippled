#include <xrpl/ledger/helpers/AMMEntry.h>

#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>

#include <gtest/gtest.h>
#include <helpers/IOU.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(AMMEntryTests, Constructors)
{
    EntryTestEnv e;

    Asset const xrp{xrpIssue()};
    Asset const usd{IOU("USD", e.alice).issue()};

    expectKeylet<AMMEntry>(e, keylet::amm(xrp, usd), "amm(asset, asset)", xrp, usd);

    expectKeylet<AMMEntry>(e, keylet::amm(e.someID()), "amm(uint256)", e.someID());
}

}  // namespace xrpl::test
