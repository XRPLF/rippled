#include <xrpl/ledger/entries/AMMEntry.h>

#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/SField.h>

#include <gtest/gtest.h>
#include <helpers/IOU.h>
#include <ledger/EntryTestHelpers.h>

#include <string>

namespace xrpl::test {

TEST(AMMEntryTests, constructors)
{
    EntryTestEnv e;

    Asset const xrp{xrpIssue()};
    Asset const usd{IOU("USD", e.alice).issue()};

    expectKeylet<AMMEntry>(e, keylet::amm(xrp, usd), "amm(asset, asset)", xrp, usd);

    for (auto const curveType : protocolCurveTypes)
    {
        expectKeylet<AMMEntry>(
            e,
            keylet::amm(xrp, usd, curveType),
            "amm(asset, asset, curveType " + std::to_string(curveType) + ")",
            xrp,
            usd,
            curveType);
    }

    expectKeylet<AMMEntry>(e, keylet::amm(e.someID()), "amm(UInt256)", e.someID());
}

TEST(AMMEntryTests, reads_each_curve_type_pool)
{
    Asset const xrp{xrpIssue()};

    for (auto const curveType : protocolCurveTypes)
    {
        EntryTestEnv e;
        Asset const usd{IOU("USD", e.alice).issue()};

        AMMEntryW created(xrp, usd, curveType, e.apply());
        ASSERT_FALSE(created.exists());
        created.newSLE();
        created->setFieldU8(sfCurveType, curveType);
        created.insert();

        AMMEntryR const read(xrp, usd, curveType, static_cast<ReadView const&>(e.apply()));
        ASSERT_TRUE(read.exists()) << "curve type " << int{curveType};
        EXPECT_EQ(read.key(), keylet::amm(xrp, usd, curveType).key);
        EXPECT_EQ(read->getFieldU8(sfCurveType), curveType);

        // The constant-product lookup finds only the constant-product pool.
        AMMEntryR const constantProduct(xrp, usd, static_cast<ReadView const&>(e.apply()));
        EXPECT_EQ(constantProduct.exists(), curveType == kCtConstantProduct)
            << "curve type " << int{curveType};
    }
}

}  // namespace xrpl::test
