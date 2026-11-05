#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/UintTypes.h>

#include <gtest/gtest.h>

#include <set>
#include <utility>
#include <vector>

namespace xrpl {

TEST(AMMKeylet, curve_types_key_distinct_pools_for_every_asset_kind)
{
    AccountID issuer;
    issuer.data()[0] = 1;
    Asset const iou{Issue{toCurrency("USD"), issuer}};
    Asset const iou2{Issue{toCurrency("EUR"), issuer}};
    Asset const mpt{MPTIssue{makeMptID(1, issuer)}};
    Asset const mpt2{MPTIssue{makeMptID(2, issuer)}};

    std::vector<std::pair<Asset, Asset>> const pairs{
        {iou, iou2},
        {iou, mpt},
        {mpt, iou},
        {mpt, mpt2},
    };

    std::set<UInt256> keys;
    for (auto const& [a, b] : pairs)
    {
        // The default curve type keys the pool as before the amendment.
        EXPECT_EQ(keylet::amm(a, b, kCtConstantProduct).key, keylet::amm(a, b).key);
        for (auto const curveType : protocolCurveTypes)
        {
            auto const key = keylet::amm(a, b, curveType).key;
            // The key does not depend on the order of the assets.
            EXPECT_EQ(key, keylet::amm(b, a, curveType).key);
            keys.insert(key);
        }
    }

    // {iou, mpt} and {mpt, iou} name the same pools; every other pair and curve
    // type names its own.
    EXPECT_EQ(keys.size(), 3 * std::size(protocolCurveTypes));
}

}  // namespace xrpl
